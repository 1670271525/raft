#include "../include/util.h"
#include <sys/stat.h>
#include <fstream>



bool File::exist(const std::string& filename){
	struct stat st;
	return stat(filename.c_str(),&st)==0;
}

void File::create_direction(const std::string& path){
	if(path.empty())return;
	if(exist(path))return;
	size_t pos,idx = 0;
	while(idx < path.size()){
		pos = path.find_first_of("/\\",idx);
		if(pos == std::string::npos){
			mkdir(path.c_str(),0777);
			return;
		}
		if(pos == idx){
			idx = pos+1;
			continue;
		}
		std::string str = path.substr(0,pos);
		if(str=="."||str==".."){
			idx = pos+1;
			continue;
		}
		if(exist(str)){
			idx = pos+1;
			continue;
		}
		mkdir(str.c_str(),0777);
		idx = pos+1;
	}
}

bool File::OpenForWrite(std::ofstream& ofs,const std::string& filename,std::ios_base::openmode mode){
	ofs.open(filename.c_str(),mode);
	if(!ofs.is_open()){
		create_direction(filename);
		ofs.open(filename.c_str(),mode);
	}
	return ofs.is_open();
}

std::string File::getContent(const std::string& path){
	std::ifstream in(path,std::ios::binary);
	if(!in.is_open())return "";
	in.seekg(0,in.end);
	std::streamsize fileSize = in.tellg();
	if(fileSize<=0)return "";

	in.seekg(0,in.beg);
	std::string content;
	try{
		content.resize(static_cast<size_t>(fileSize));
		if(!in.read(&content[0],fileSize))return "";
	}catch(const std::bad_alloc& e){
		return "";
	}
	
	return content;
}

/**
 * 获取目录下匹配前缀或后缀的文件名
 * @param path 目标目录路径
 * @param prefix 匹配前缀 (如 "log_")
 * @param suffix 匹配后缀 (如 ".txt")
 */
std::vector<std::string> File::getFilesC11(const std::string& path, 
                                     const std::string& prefix, 
                                     const std::string& suffix) {
    std::vector<std::string> fileList;
    DIR* dir = opendir(path.c_str());
    
    if (dir == nullptr) {
        return fileList; // 目录打开失败
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // 1. 过滤掉 "." 和 ".." 以及文件夹
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // 注意：在某些文件系统上 d_type 可能不可靠，这里仅作简单过滤
        if (entry->d_type == DT_DIR) {
            continue;
        }

        std::string fileName(entry->d_name);
        bool isMatch = false;

        // 2. 检查前缀
        if (!prefix.empty()) {
            if (fileName.size() >= prefix.size() && 
                fileName.compare(0, prefix.size(), prefix) == 0) {
                isMatch = true;
            }
        }

        // 3. 检查后缀 (如果还没匹配成功)
        if (!isMatch && !suffix.empty()) {
            if (fileName.size() >= suffix.size() && 
                fileName.compare(fileName.size() - suffix.size(), suffix.size(), suffix) == 0) {
                isMatch = true;
            }
        }

        if (isMatch) {
            fileList.push_back(path + "/" + fileName);
        }
    }

    closedir(dir); // 必须手动关闭，POSIX 不支持 RAII 自动关闭
    return fileList;
}
