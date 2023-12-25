#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <curl/curl.h>
#include <iostream>
#include <fstream>
#include <errno.h>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

namespace fs = std::filesystem;

using json = nlohmann::json;

struct FileConfig {
    std::string name;
    std::string local_path;
    std::string server_url;
};

struct FileData {
    std::string fileName;
    std::string sha256;
};

std::string extractVersionFromFilename(const std::string& filename) {
    size_t underscorePos = filename.find_last_of('_');
    if (underscorePos != std::string::npos && underscorePos + 1 < filename.length()) {
        return filename.substr(underscorePos + 1);
    }
    return ""; // 或者返回一个表示错误的特定字符串
}

std::vector<std::string> findFilesWithPrefix(const std::string& directory, const std::string& prefix) {
    std::vector<std::string> matchedFiles;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().filename().string().find(prefix) == 0) {
            matchedFiles.push_back(entry.path().filename().string());
        }
    }
    return matchedFiles;
}

std::vector<std::string> splitString(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);

    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }

    return tokens;
}

bool isVersionGreater(const std::string& v1, const std::string& v2) {
    auto splitVersion = [](const std::string& version) {
        size_t hyphenPos = version.find('-');
        std::string baseVersion = version.substr(0, hyphenPos);
        std::string preRelease = hyphenPos != std::string::npos ? version.substr(hyphenPos + 1) : "";

        auto parts = splitString(baseVersion, '.');
        return std::make_tuple(
            parts.size() > 0 ? std::stoi(parts[0]) : 0,
            parts.size() > 1 ? std::stoi(parts[1]) : 0,
            parts.size() > 2 ? std::stoi(parts[2]) : 0,
            preRelease
        );
    };

    auto [major1, minor1, patch1, preRelease1] = splitVersion(v1);
    auto [major2, minor2, patch2, preRelease2] = splitVersion(v2);

    if (major1 != major2) return major1 > major2;
    if (minor1 != minor2) return minor1 > minor2;
    if (patch1 != patch2) return patch1 > patch2;

    // 如果一个版本有预发布标识符而另一个没有，则没有预发布标识符的版本更大
    if (preRelease1.empty() && !preRelease2.empty()) return true;
    if (!preRelease1.empty() && preRelease2.empty()) return false;

    // 如果两个版本都有预发布标识符，则按字典序比较
    return preRelease1 > preRelease2;
}

std::string findLatestVersionFile(const FileConfig& config) {
    auto files = findFilesWithPrefix(config.local_path, config.name);
    std::string latestVersion;
    std::string latestFile;

    for (const auto& file : files) {
        std::string version = extractVersionFromFilename(file);
        if (latestVersion.empty() || isVersionGreater(version, latestVersion)) {
            latestVersion = version;
            latestFile = file;
        }
    }

    if (files.empty()) {
        return "";
    }


    return latestFile;
}

std::vector<FileConfig> parseConfig(const std::string& configPath) {
    YAML::Node config = YAML::LoadFile(configPath);
    std::vector<FileConfig> files;

    for (const auto& fileNode : config["files"]) {
        FileConfig fileConfig;
        fileConfig.name = fileNode["name"].as<std::string>();
        fileConfig.local_path = fileNode["local_path"].as<std::string>(); // 更新为 local_path
        fileConfig.server_url = fileNode["server_url"].as<std::string>();
        files.push_back(fileConfig);
    }

    return files;
}

static size_t CurlWrite_CallbackFunc_StdString(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
    } catch(std::bad_alloc &e) {
        // handle memory problem
        return 0;
    }
    return newLength;
}

bool isServerVersionNewer(const std::string& serverUrl, const std::string& localVersion, const std::string& name) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;
    json j;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, serverUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite_CallbackFunc_StdString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            std::cout << "ERROR SERVER VERSION" << std::endl;
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        } else {
            j = json::parse(readBuffer);
        }
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();

    std::string latestServerVersion;
    std::string latestServerFile;

    if(!j.empty()) {
        for (const auto& item : j) {
            std::string title = item["title"];
            if (title.find(name) == 0) {
                std::string version = extractVersionFromFilename(title);
                if (latestServerVersion.empty() || isVersionGreater(version, latestServerVersion)) {
                    latestServerVersion = version;
                    latestServerFile = title;
                }
            }
        }
    }

    return isVersionGreater(latestServerVersion, localVersion);
}


static size_t WriteData(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    // std::cout.write(static_cast<char*>(ptr), size * nmemb);
    size_t written = fwrite(ptr, size, nmemb, stream);
    std::cout << "WRITTEN: " << written << std::endl;
    return written;
}

FileData getLatestServerFile(const std::string& serverUrl, const std::string& name) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;
    json j;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if(curl) {
        std::cout << "SERVER URL: " << serverUrl << std::endl; // TODO: 用日志库替换 "cout
        curl_easy_setopt(curl, CURLOPT_URL, serverUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite_CallbackFunc_StdString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            std::cout << "ERROR GET LATEST SERVER FILE" << std::endl; // TODO: 用日志库替换 "cout
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        } else {
            std::cout << "READ BUFFER: " << readBuffer << std::endl; // TODO: 用日志库替换 "cout
            j = json::parse(readBuffer);
        }
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();

    std::string latestServerVersion;
    std::string latestServerFile;
    FileData latestFileData;

    std::cout << "JSON: " << j << std::endl; // TODO: 用日志库替换 "cout
    if (!j.empty()) {
        for (const auto& item : j) {
            std::string title = item["title"];
            std::string sha256 = item["sha256"];
            if (title.find(name) == 0) {
                std::string version = extractVersionFromFilename(title);
                if (latestServerVersion.empty() || isVersionGreater(version, latestServerVersion)) {
                    latestServerVersion = version;
                    latestFileData.fileName = title;
                    latestFileData.sha256 = sha256;
                }
            }
        }
    }

    return latestFileData;
}

std::string getSHA256Sum(const std::string& filePath) {
    std::string command = "sha256sum " + filePath + " 2>&1"; // 2>&1 以确保错误信息也被捕获
    std::array<char, 128> buffer;
    std::string result;

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return "ERROR: Unable to open the pipe.";
    }

    try {
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            result += buffer.data();
        }
    } catch (...) {
        pclose(pipe);
        throw;
    }

    pclose(pipe);

    // 只获取 SHA256 哈希部分，不包括文件名
    auto spacePos = result.find(' ');
    if (spacePos != std::string::npos) {
        result = result.substr(0, spacePos);
    }

    return result;
}

std::string downloadAndReplaceFile(const std::string& url, const std::string& localFilePath) {
    CURL *curl;
    FILE *fp;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (curl) {
        fp = fopen(localFilePath.c_str(), "wb");
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteData);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        fclose(fp);
    }
    curl_global_cleanup();

    if (res != CURLE_OK) {
        std::cout << "ERROR DOWNLOAD" << std::endl;
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        // Handle the error or throw an exception
    }
    // TODO:  最好使用系统库来实现哈希散列计算，而不是调用外部命令
    // usleep(1000000); // 1s

    // // 计算文件的 SHA256 哈希值
    // unsigned char hash[SHA256_DIGEST_LENGTH];
    // SHA256_CTX sha256;
    // SHA256_Init(&sha256);

    // std::ifstream file(localFilePath, std::ifstream::binary);
    // if (file) {
    //     char buf[32768];
    //     while (file.read(buf, sizeof(buf))) {
    //         SHA256_Update(&sha256, buf, file.gcount());
    //     }
    //     SHA256_Final(hash, &sha256);
    // } else {
    //     std::cerr << "Unable to open downloaded file for hashing." << std::endl;
    //     return ""; // 或者抛出异常
    // }

    // // 将哈希值转换为十六进制字符串
    // std::stringstream ss;
    // for (unsigned char c : hash) {
    //     ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    // }
    // std::string fileHash = ss.str();

    // // 输出哈希值用于调试
    // std::cout << "SHA256 Hash: " << fileHash << std::endl;
    std::cout << "SHA256 output: " << getSHA256Sum(localFilePath) << std::endl;
    return getSHA256Sum(localFilePath);
}



void updateConfigVersion(const std::string& configPath, const std::string& name, const std::string& newVersion) {
    YAML::Node config = YAML::LoadFile(configPath);

    for (auto fileNode : config["files"]) {
        if (fileNode["name"].as<std::string>() == name) {
            fileNode["local_version"] = newVersion;
            break;
        }
    }

    std::ofstream fout(configPath);
    fout << config;
}

bool isProcessRunning(const std::string& processName) {
    std::ostringstream cmd;
    cmd << "pgrep " << processName;
    return system(cmd.str().c_str()) == 0;
}

void terminateProcess(const std::string& processName) {
    std::ostringstream cmd;
    cmd << "pkill " << processName;
    int result = system(cmd.str().c_str());
    if (result != 0) {
        std::cerr << "Failed to terminate process: " << processName << std::endl;
    }
}

std::string createNewVersionPath(const std::string& localPath, const std::string& name, const std::string& serverVersion) {
    return localPath + name + "_" + serverVersion;
}

bool startNewProgram(const std::string& programPath) {
    pid_t pid = fork();

    if (pid == -1) {
        // Fork 失败
        std::cerr << "Failed to fork a new process." << std::endl;
        return false;
    } else if (pid > 0) {
        // 父进程
        int status;
        waitpid(pid, &status, 0); // 等待子进程结束
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    } else {
        // 子进程
        execl(programPath.c_str(), programPath.c_str(), (char*)NULL);
        // 如果 execl 成功，下面的代码不会被执行
        _exit(EXIT_FAILURE); // execl 失败，退出子进程
    }
}

bool setExecutablePermission(const std::string& filePath) {
    if (chmod(filePath.c_str(), S_IRWXU | S_IRWXG | S_IRWXO) == -1) {
        std::cerr << "Failed to set executable permission for " << filePath << std::endl;
        return false;
    }
    return true;
}

int main() {
    const std::string configPath = "/home/luo980/mygits/file-update-ota/src/daemon/config/config.yaml";
    auto files = parseConfig(configPath);

    for (const auto& file : files) {
        // 查找本地目录中最新版本的文件名
        std::string latestLocalFile = findLatestVersionFile(file);
        std::string localVersion = latestLocalFile.empty() ? "" : extractVersionFromFilename(latestLocalFile);

        // 从服务器获取最新版本的文件名和版本号
        FileData latestFileData = getLatestServerFile(file.server_url, file.name);
        std::string serverVersion = extractVersionFromFilename(latestFileData.fileName);

        std::cout << "LOCAL VERSION: " << localVersion << std::endl;
        std::cout << "SERVER VERSION: " << serverVersion << std::endl;

        // std::cout << "LOCAL Version > SERVER: " << isVersionGreater(localVersion, serverVersion) << " " << std::endl;
        // 比较版本号，检查是否需要更新
        if (isVersionGreater(serverVersion, localVersion)) {
            std::cout << "SERVER VERSION IS NEWER" << std::endl;
            // 构建下载 URL 和本地保存路径
            std::string downloadUrl = file.server_url + latestFileData.fileName;
            std::string localFilePath = file.local_path  + latestFileData.fileName;

            // 清理目标进程
                if (isProcessRunning(file.name)) {
                    terminateProcess(file.name);
                }

            // 下载并替换文件
            std::cout << "DOWNLOADING" << std::endl;
            std::string localHash = downloadAndReplaceFile(downloadUrl, localFilePath);
            if(localHash != latestFileData.sha256 ) {
                std::cout << "ERROR HASH" << std::endl;
                std::cerr << "Hash mismatch for downloaded file." << std::endl;
                int retry = 0;
                while (retry < 3) {
                    std::cout << "RETRY: " << retry << std::endl;
                    localHash = downloadAndReplaceFile(downloadUrl, localFilePath);
                    if (localHash == latestFileData.sha256) {
                        break;
                    }
                    retry++;
                }
                if (retry == 3) {
                    std::cout << "ERROR RETRY" << std::endl;
                    std::cerr << "Failed to download file after 3 retries." << std::endl;
                    if (std::remove(localFilePath.c_str()) != 0) {
                        std::cerr << "Failed to delete corrupted file: " << localFilePath << std::endl;
                    } else {
                        std::cout << "Deleted corrupted file: " << localFilePath << std::endl;
                    }
                    return -1;
                }
            }

            std::cout << "HASH OK" << std::endl;
            // 更新配置文件
            std::cout << "UPDATING CONFIG" << std::endl;
            updateConfigVersion(configPath, file.name, serverVersion);

            std::cout << "Start Program" << std::endl;
            // 启动最新版本的程序
            std::cout << "Starting new version of the program..." << std::endl;
            std::string newVersionPath = createNewVersionPath(file.local_path, file.name, serverVersion);
            // 设置权限
            setExecutablePermission(newVersionPath);
            // bool result = std::system(newVersionPath.c_str());
            startNewProgram(newVersionPath);
            // if (result != 0) {
            //     std::cerr << "Failed to start new version of the program." << std::endl;
            //     return 1;
            // }
        }
    }
    return 0;
}