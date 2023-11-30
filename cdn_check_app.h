#include <curl/curl.h>
#include <string>
#include <vector>

class CDNCheckApp {
public:
  CDNCheckApp(std::string &ch_map_path, std::string stats_path,
                                    uint32_t iterations, uint32_t max_bitrate);
  bool checkAndProfileCDNs();

private:
  struct AssetInfo {
    std::string url;
    std::string ch_name;
    std::string ch_id;
  };

  struct MediaManifest {
    std::string url;
    uint32_t bitrate = 0;
  };

  struct CurlUserData {
    CurlUserData(int type):buf_type(type) {}
    std::string buffer;
    int buf_type; //0: Main Manifest, 1: Media Manifest 2: Segment
  };

  static size_t write_data(void *ptr, size_t size, size_t nmemb, void *udata);
  std::string construct_url(const std::string &parent, const std::string &child);
  void get_stats(CURL *curl, double *duration, long *redirects);
  void dump_stats(std::string &stats_path, AssetInfo &asset_info,
                                            double *duration, long *redirects);
  bool read_asset_file(std::string &file_name, std::vector<unsigned char> &out_buf);
  std::vector<AssetInfo> get_asset_map(std::string &ch_map_path);
  std::string get_segment_url(std::string &media_manifest,
                               uint32_t offset_from_head = 1 /*second last*/);
  std::string get_media_manifest_url(std::string &main_manifest);

private:
    const char *kSchemaHttps = "https://";
    const char *kSchemaHttp = "http://";
    std::string stats_path_;
    uint32_t iterations_;
    uint32_t max_bitrate_;
    std::vector<AssetInfo> ch_map_;
};
