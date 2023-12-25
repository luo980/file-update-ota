# OTA Tool

共包含3个相关子仓库，拉取时注意submodule

[demo](https://github.com/luo980/ota_demo)
[vue frontend](https://github.com/vercel/serve)
[HTTP Fileserver](https://github.com/luo980/HTTPFileServer)

其中demo可用于版本测试; vue frontend可以展示文件服务前端，但需要手动刷新，未修改官方源码，可不用; HTTP Fileserver为fork修订版，添加了sha校验支持

相关库依赖：openssl, libcurl, nlohmann json, yaml-cpp

## demo部分

demo部分主要通过CMake结合git实现版本号控制，通过读取git branch来编译出指定版本的二进制程序

## uploader部分

uploader为HTTP Fileserver的简单上传后端，通过标准PUT语义基于form-data格式body实现文件上传，使用方式为：uploader <URL> <FilePath>，URL为服务器路径，FilePath为本地文件路径

建议直接g++/clang++编译，g++ -o uploader uploader.cpp -lcurl即可

## Vue Frontend部分

该部分为一个包含http服务的前端，安装参考官方npm install --global serve即可，使用serve <Path>即可开启带有前端的文件服务器，默认端口为3000

## HTTP Fileserver部分

本项目中主要承担OTA文件服务器部分，不带前端，纯API操作，在原作者基础上添加了额外SHA256sum校验过程，保证了文件的可靠性。

使用方法：使用go install 安装以下两个go依赖库

"github.com/labstack/echo"

"github.com/labstack/gommon/log"

后通过go build对应main.go即可得到可执行文件

在main函数中可指定服务端口
```
e.Logger.Fatal(e.Start(":1323"))
```

在文件头const部分dir字段可指定文件服务路径
```
const (
	dir 			= 		"/home/luo980/mygits/file-update-ota/serve_path"
	FILE 			= 		"File"
	FOLDER 		= 		"Folder"
)
```
其中File结构体部分定义了通过GET方法返回json结构
```
type File struct {
	ID				int 	`json:"id"`
	Title			string	`json:"title"`
	Type			string	`json:"type"`
	SHA256			string	`json:"sha256"`
}
```
返回样例：
```
{
    "id": 0,
    "title": "camera_ability_2.5.0-beta",
    "type": "File",
    "sha256": "efe6202ad43e9e6841f6b664a01f9636e1d033f4ceefbfaa3caa4ea234a2f739"
}
```
如想通过可视化测试上传，可参考以下PUT过程，在Body段选择File类型，然后Value部分选择本地文件
![image](https://github.com/luo980/file-update-ota/assets/12494413/51e62888-27d4-4ea9-9f05-b7a2b8a37058)

如想通过可视化测试获取文件列表信息，可直接通过GET对目标路径发起请求即可
![image](https://github.com/luo980/file-update-ota/assets/12494413/9b96a5cc-e6ad-453d-afec-45f862bf8208)

## daemon部分

### config定义

格式如下：

```
files:
  - name: camera_ability
    local_path: /home/luo980/mygits/file-update-ota/local/
    server_url: http://localhost:1323/ability_unit/
    local_version: 3.0.0-alpha
```
其中：
* name：二进制名称，不附带版本号
* local_path: 本地绝对路径
* server_url: 目标服务器文件夹路径
* local_version: 本地二进制版本（如本地无文件则无效）

### 程序使用

通过CMake编译即可，依赖齐全可正常编译，daemon开始执行后会按照config中项目列表进行遍历检查，可考虑后续使用过程让daemon定时执行，或者定期检测config文件是否发生修改来触发执行，未测试多个项目同时更改后被更新和拉起，建议后续使用过程中完善。



