#include <iostream>
#include <string>
#include <curl/curl.h>
#include <sys/stat.h>
#include <unistd.h>

static size_t read_callback(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t retcode = fread(ptr, size, nmemb, stream);
    return retcode;
}

bool uploadFile(const std::string& url, const std::string& filePath) {
    CURL *curl;
    CURLcode res;
    curl_mime *form = NULL;
    curl_mimepart *field = NULL;

    // 初始化 libcurl
    curl_global_init(CURL_GLOBAL_ALL);

    // 初始化 CURL 句柄
    curl = curl_easy_init();
    if(curl) {
        // 创建一个 MIME 表单
        form = curl_mime_init(curl);

        // 添加文件部分
        field = curl_mime_addpart(form);
        curl_mime_name(field, "File");
        curl_mime_filedata(field, filePath.c_str());

        // 设置目标 URL
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");

        // 执行请求
        res = curl_easy_perform(curl);

        // 检查错误
        if(res != CURLE_OK) {
            std::cout << "ERROR HERE?" << std::endl;
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        }

        // 清理
        curl_easy_cleanup(curl);
        curl_mime_free(form);
    }
    curl_global_cleanup();
    return res == CURLE_OK;
}


int main(int argc, char** argv) {
    if(argc < 3) {
        std::cout << "Usage: " << argv[0] << " <URL> <FilePath>" << std::endl;
        return 1;
    }

    std::string url = argv[1]; // 从命令行获取 URL
    std::string filePath = argv[2]; // 从命令行获取文件路径

    if(uploadFile(url, filePath)) {
        std::cout << "File uploaded successfully" << std::endl;
    } else {
        std::cout << "File upload failed" << std::endl;
    }

    return 0;
}