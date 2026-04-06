#pragma once
#include <fstream>
#include <string>
#include <vector>
#include <dirent.h>
#include <cstring>


class File{
public:
	static bool exist(const std::string& filename);
	static std::string path(const std::string& name);
	static void create_direction(const std::string& path);
	static bool OpenForWrite(std::ofstream& ofs,const std::string& filename,std::ios_base::openmode mode);
	static std::string getContent(const std::string& path);
	static std::vector<std::string> getFilesC11(const std::string& path, 
                                     const std::string& prefix, 
                                     const std::string& suffix);


};


