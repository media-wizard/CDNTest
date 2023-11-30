// To build: g++ -std=c++17 cdn_check_app.cpp -o cdn_check_app -lcurl

#include "cdn_check_app.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "nlohmann/json.hpp"
#include <sstream>
#include <stdio.h>

size_t CDNCheckApp::write_data(void *ptr, size_t size, size_t nmemb,
                                                                 void *udata) {
  CurlUserData *user_data = (CurlUserData *)udata;
  if (user_data->buf_type < 2) {
    user_data->buffer.append((char *)ptr, size * nmemb);
  }
  return size * nmemb;
}

std::string CDNCheckApp::construct_url(const std::string &parent,
                                                    const std::string &child) {
  size_t parent_end = parent.find("?"); //Either end of URL or start of params
  size_t parent_file_start = parent.rfind('/', parent_end);
  std::string new_url(parent.substr(0,parent_file_start+1)); //Including '/'
  new_url += child;
  return new_url;
}

void CDNCheckApp::get_stats(CURL *curl, double *duration, long *redirects) {
  CURLcode res = curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME, duration);
  // printf("Name lookup Done: %.3f\n", *duration);
  duration++;
  res = curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, duration);
  // printf("TCP Connect Done: %.3f\n", *duration);
  duration++;
  res = curl_easy_getinfo(curl, CURLINFO_APPCONNECT_TIME, duration);
  // printf("App Connect Done: %.3f\n", *duration);
  duration++;
  res = curl_easy_getinfo(curl, CURLINFO_PRETRANSFER_TIME, duration);
  // printf("Data Request Sent: %.3f\n", *duration);
  duration++;
  res = curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME, duration);
  // printf("Transfer Start: %.3f\n", *duration);
  duration++;
  res = curl_easy_getinfo(curl, CURLINFO_REDIRECT_TIME, duration);
  // printf("Redirect(s): %.3f\n", *duration);
  duration++;
  res = curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, duration);

  res = curl_easy_getinfo(curl, CURLINFO_REDIRECT_COUNT, redirects);
  // printf("Reditect Count: %ld\n", *redirects);
  // printf("End Transfer: %.3f\n\n", *duration);
}

void CDNCheckApp::dump_stats(std::string &stats_path,
                AssetInfo &asset_info, double *duration, long *redirects) {
  std::string file_path(stats_path + "/" + asset_info.ch_id + ".csv");
  double gand_total = 0.0;
  std::stringstream stats;
  stats << std::fixed;
  stats << std::setprecision(4);
  for (int i = 0; i < 3; i++) {
    stats << duration[0] << ",";
    stats << duration[1] << ",";
    stats << duration[2] << ",";
    stats << duration[3] << ",";
    stats << duration[4] << ",";
    stats << duration[5] << ",";
    stats << redirects[i] << ",";
    stats << duration[6] << ",";
    gand_total += duration[6];
    duration += 7;
  }
  stats << gand_total << "\n";

  std::ofstream stats_file(file_path, std::ios::app);
  stats_file << stats.str();
}

bool CDNCheckApp::read_asset_file(std::string &file_name,
                                         std::vector<unsigned char> &out_buf) {
  bool ret = false;
  FILE *file = fopen(file_name.c_str(), "r");
  if (file) {
    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET);

    out_buf.resize(fsize + 1);
    fread(out_buf.data(), fsize, 1,file);
    fclose(file);
    ret = true;
  } else {
    std::cout << "Not able open asset map file.\n";
  }
  return ret;
}

std::vector<CDNCheckApp::AssetInfo>
                       CDNCheckApp::get_asset_map(std::string &ch_map_path) {
  std::vector<AssetInfo> ch_map;
  std::vector<unsigned char> file_buf;
  if (read_asset_file(ch_map_path, file_buf)) {
    auto json = nlohmann::json::parse(file_buf, nullptr, false);
    if (json.is_discarded())
    {
      std::cout << "Invalid JSON\n";
    } else {
      for (const auto &j_obj : json["assets"].items()) {
        AssetInfo ch_info;
        bool ch_id_found = false;
        auto &ch =  j_obj.value();
        auto i_id = ch.find("AssetId");
        if (i_id != ch.end()) {
          ch_info.ch_id = std::string{*i_id};
          ch_id_found = true;
        }

        auto i_name = ch.find("AssetName");
        if (i_name != ch.end()) {
          ch_info.ch_name = std::string{*i_name};
        }

        auto i_urls = ch.find("AssetUrls");
        if (ch_id_found && i_urls != ch.end() && i_urls->is_array()
                                                       && !(i_urls->empty())) {
          ch_info.url = *i_urls->begin();
          ch_map.push_back(std::move(ch_info));
        }
      }
    }
  }
  return ch_map;
}

std::string CDNCheckApp::get_media_manifest_url(std::string &main_manifest) {
  MediaManifest selected_media_manifest;
  std::stringstream main_ss(main_manifest);
  std::string line;
  while (std::getline(main_ss, line)) {
    if (std::string::npos != line.find("#EXT-X-STREAM-INF")) {
      MediaManifest mm;
      size_t pos = line.find("BANDWIDTH="); // may hit AVERAGE-BANDWIDTH too
      if (std::string::npos != pos) {
        pos += std::string("BANDWIDTH=").length();
        size_t end_pos = line.find(",", pos);
        size_t len = std::string::npos;
        if (end_pos != std::string::npos)
          len = end_pos - pos;
        mm.bitrate = strtoul(line.substr(pos, len).c_str(), NULL, 10);
        if (mm.bitrate > max_bitrate_ ||
                       mm.bitrate <= selected_media_manifest.bitrate) continue;
      }
      // Next line is media manifest URL
      if (std::getline(main_ss, line)) {
        mm.url = line;
        std::swap(selected_media_manifest, mm);
      }
    }
  }
  return selected_media_manifest.url;
}

std::string CDNCheckApp::get_segment_url(std::string &media_manifest,
                                                uint32_t offset_from_head) {
  std::vector<std::string> seg_urls;

  std::stringstream media_ss(media_manifest);
  std::string line;
  while (std::getline(media_ss, line)) {
    if (std::string::npos != line.find("#EXTINF:")) {
      // Next line is segment URL
      if (std::getline(media_ss, line)) {
        seg_urls.push_back(line);
      }
    }
  }
  if (!seg_urls.empty()) {
    size_t selected_idx = (offset_from_head >= seg_urls.size()) ?
                                       0: (seg_urls.size() - offset_from_head);
    return seg_urls[selected_idx];
  }
  return std::string("");
}

CDNCheckApp::CDNCheckApp(std::string &ch_map_path, std::string stats_path,
                                  uint32_t iterations, uint32_t max_bitrate) :
                            stats_path_(stats_path), iterations_(iterations),
                                                    max_bitrate_(max_bitrate) {
  ch_map_ = get_asset_map(ch_map_path);
}

bool CDNCheckApp::checkAndProfileCDNs() {
  if (ch_map_.empty()) {
    std::cout << "Empty Asset Map.\n";
    return false;
  }

  for(uint32_t iter = 0; iter < iterations_; iter++) {
    for (auto &asset : ch_map_) {
      CURL *curl = curl_easy_init();
      if (curl) {
        std::string url = asset.url;
        double time[3 * 7]; // 3 : Main manifest, Media Manifest, First Segment
        long redirects[3];
        int phase = 0;
        std::cout << "Downloading: \"" << asset.ch_name << "\", Iteration:" << iter << std::endl;
        for (; phase<3; phase++) {
          CurlUserData user_data(phase);
          memset(time, (sizeof(double) * 3 * 7), 0);
          memset(redirects, (sizeof(long) * 3), 0);
          curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
          curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
          curl_easy_setopt(curl, CURLOPT_WRITEDATA, &user_data);
          curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &CDNCheckApp::write_data);
          curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
          std::string next_url;
          CURLcode res = curl_easy_perform(curl);
          if (CURLE_OK == res) {
            get_stats(curl, &(time[phase * 7]), &(redirects[phase]));
            if (redirects[phase]) {
              // Redirected to another URL. Get new URL
              char *eff_url = NULL;
              curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff_url);
              url = eff_url;
            }
            switch (phase) {
              case 0: // Main Manifest done
                next_url = get_media_manifest_url(user_data.buffer);
                break;
              case 1: // Media Manifest done
                next_url = get_segment_url(user_data.buffer);
                break;
              default:
                dump_stats(stats_path_, asset, time, redirects);
                break;
            }
          } else {
            std::cout << "Failed Phase[" << phase << "], URL:" << url << std::endl;
            break;
          }
          if (phase < 2 && !next_url.empty() &&
              0 != next_url.find(kSchemaHttps) &&
              0 != next_url.find(kSchemaHttp)) {
            next_url = construct_url(url, next_url);
          }
          if (phase < 2 && next_url.empty()) {
            std::cout << "Couldn't find URL for Phase[" << phase+1 << "]\n";
            break;
          }
          url = next_url;
        }
        curl_easy_cleanup(curl);
      }
    }
  }

  return true;
}

int main(int argc, char *argv[]) {
  std::string ch_map_path("--help");
  std::string stats_path("./");
  uint32_t iterations{100};
  uint32_t max_bitrate{2500000};
  if (argc > 4) {
    max_bitrate = strtoul(argv[4], NULL, 10);
  }
  if (argc > 3) {
    iterations = strtoul(argv[3], NULL, 10);
  }
  if (argc > 2) {
    stats_path = argv[2];
  }
  if (argc > 1) {
    ch_map_path = argv[1];
  }
  if (0 == ch_map_path.find("--help") || 0 == ch_map_path.find("-h")) {
    printf("Syntax:\t%s asset_map.json [iter] [dir] [bps]\n"
        "\t\tdir : out dir for writing stats, default - cur directory\n"
        "\t\titer : Number of downloads for each streams, default - 100\n"
        "\t\tbps : max bitrate in bps, default - 2500000\n", argv[0] );
    return -1;
  }
  if (0 != stats_path.compare("./")) {
    std::filesystem::remove_all(stats_path);
    std::filesystem::create_directories(stats_path);
  }
  CDNCheckApp cdn_check_app {ch_map_path, std::move(stats_path),
                                                      iterations, max_bitrate};
  cdn_check_app.checkAndProfileCDNs();
  return 0;
}
