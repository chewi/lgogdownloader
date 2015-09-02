/* This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details. */

#include "util.h"

#include <boost/filesystem.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <tinyxml.h>
#include <json/json.h>
#include <fstream>
#include <sys/ioctl.h>

/*
    Create filepath from specified directory and path
    Remove the leading slash from path if needed
    Use gamename as base directory if specified
*/
std::string Util::makeFilepath(const std::string& directory, const std::string& path, const std::string& gamename, std::string subdirectory, const unsigned int& platformId, const std::string& dlcname)
{
    std::string dir = directory + makeRelativeFilepath(path, gamename, subdirectory);
    Util::filepathReplaceReservedStrings(dir, gamename, platformId, dlcname);
    return dir;
}

/* Create filepath relative to download base directory specified in config.
 */
std::string Util::makeRelativeFilepath(const std::string& path, const std::string& gamename, std::string subdirectory)
{
    std::string filepath;

    if (gamename.empty())
    {
        if (path.at(0)=='/')
        {
            std::string tmp_path = path.substr(1,path.length());
            filepath = tmp_path;
        }
        else
        {
            filepath = path;
        }
    }
    else
    {
        std::string filename = path.substr(path.find_last_of("/")+1, path.length());
        if (!subdirectory.empty())
        {
            subdirectory = "/" + subdirectory;
        }
        filepath = subdirectory + "/" + filename;
    }

    return filepath;
}

std::string Util::getFileHash(const std::string& filename, unsigned hash_id)
{
    unsigned char digest[rhash_get_digest_size(hash_id)];
    char result[rhash_get_hash_length(hash_id)];

    rhash_library_init();
    int i = rhash_file(hash_id, filename.c_str(), digest);
    if (i < 0)
        std::cout << "LibRHash error: " << strerror(errno) << std::endl;
    else
        rhash_print_bytes(result, digest, rhash_get_digest_size(hash_id), RHPR_HEX);

    return result;
}

std::string Util::getChunkHash(unsigned char *chunk, uintmax_t chunk_size, unsigned hash_id)
{
    unsigned char digest[rhash_get_digest_size(hash_id)];
    char result[rhash_get_hash_length(hash_id)];

    rhash_library_init();
    int i = rhash_msg(hash_id, chunk, chunk_size, digest);
    if (i < 0)
        std::cout << "LibRHash error: " << strerror(errno) << std::endl;
    else
        rhash_print_bytes(result, digest, rhash_get_digest_size(hash_id), RHPR_HEX);

    return result;
}

// Create GOG XML
int Util::createXML(std::string filepath, uintmax_t chunk_size, std::string xml_dir)
{
    int res = 0;
    FILE *infile;
    FILE *xmlfile;
    uintmax_t filesize, size;
    int chunks, i;

    if (xml_dir.empty())
    {
        xml_dir = Util::getCacheHome() + "/lgogdownloader/xml";
    }

    // Make sure directory exists
    boost::filesystem::path path = xml_dir;
    if (!boost::filesystem::exists(path)) {
        if (!boost::filesystem::create_directories(path)) {
            std::cout << "Failed to create directory: " << path << std::endl;
            return res;
        }
    }

    if ((infile=fopen(filepath.c_str(), "r"))!=NULL) {
        //File exists
        fseek(infile, 0, SEEK_END);
        filesize = ftell(infile);
        rewind(infile);
    } else {
        std::cout << filepath << " doesn't exist" << std::endl;
        return res;
    }

    // Get filename
    boost::filesystem::path pathname = filepath;
    std::string filename = pathname.filename().string();
    std::string filenameXML = xml_dir + "/" + filename + ".xml";

    std::cout << filename << std::endl;
    //Determine number of chunks
    int remaining = filesize % chunk_size;
    chunks = (remaining == 0) ? filesize/chunk_size : (filesize/chunk_size)+1;
    std::cout   << "Filesize: " << filesize << " bytes" << std::endl
                << "Chunks: " << chunks << std::endl
                << "Chunk size: " << (chunk_size >> 20) << " MB" << std::endl;

    TiXmlDocument xml;
    TiXmlElement *fileElem = new TiXmlElement("file");
    fileElem->SetAttribute("name", filename);
    fileElem->SetAttribute("chunks", chunks);
    fileElem->SetAttribute("total_size", std::to_string(filesize));

    std::cout << "Getting MD5 for chunks" << std::endl;

    rhash rhash_context;
    rhash_library_init();
    rhash_context = rhash_init(RHASH_MD5);
    if(!rhash_context)
    {
        std::cerr << "error: couldn't initialize rhash context" << std::endl;
        return res;
    }
    char rhash_result[rhash_get_hash_length(RHASH_MD5)];

    for (i = 0; i < chunks; i++) {
        uintmax_t range_begin = i*chunk_size;
        fseek(infile, range_begin, SEEK_SET);
        if ((i == chunks-1) && (remaining != 0))
            chunk_size = remaining;
        uintmax_t range_end = range_begin + chunk_size - 1;
        unsigned char *chunk = (unsigned char *) malloc(chunk_size * sizeof(unsigned char *));
        if (chunk == NULL)
        {
            std::cout << "Memory error" << std::endl;
            return res;
        }
        size = fread(chunk, 1, chunk_size, infile);
        if (size != chunk_size)
        {
            std::cout << "Read error" << std::endl;
            free(chunk);
            return res;
        }

        std::string hash = Util::getChunkHash(chunk, chunk_size, RHASH_MD5);
        rhash_update(rhash_context, chunk, chunk_size); // Update hash for the whole file

        free(chunk);

        TiXmlElement *chunkElem = new TiXmlElement("chunk");
        chunkElem->SetAttribute("id", i);
        chunkElem->SetAttribute("from", std::to_string(range_begin));
        chunkElem->SetAttribute("to", std::to_string(range_end));
        chunkElem->SetAttribute("method", "md5");
        TiXmlText *text = new TiXmlText(hash);
        chunkElem->LinkEndChild(text);
        fileElem->LinkEndChild(chunkElem);

        std::cout << "Chunks hashed " << (i+1) << " / " << chunks << "\r" << std::flush;
    }
    fclose(infile);

    rhash_final(rhash_context, NULL);
    rhash_print(rhash_result, rhash_context, RHASH_MD5, RHPR_HEX);
    rhash_free(rhash_context);

    std::string file_md5 = rhash_result;
    std::cout << std::endl << "MD5: " << file_md5 << std::endl;
    fileElem->SetAttribute("md5", file_md5);

    xml.LinkEndChild(fileElem);

    std::cout << "Writing XML: " << filenameXML << std::endl;
    if ((xmlfile=fopen(filenameXML.c_str(), "w"))!=NULL) {
        xml.Print(xmlfile);
        fclose(xmlfile);
        res = 1;
    } else {
        std::cout << "Can't create " << filenameXML << std::endl;
        return res;
    }

    return res;
}

/*
    Overrides global settings with game specific settings
    returns 0 if fails
    returns number of changed settings if succesful
*/
int Util::getGameSpecificConfig(std::string gamename, gameSpecificConfig* conf, std::string directory)
{
    int res = 0;

    if (directory.empty())
    {
        directory = Util::getConfigHome() + "/lgogdownloader/gamespecific";
    }

    std::string filepath = directory + "/" + gamename + ".conf";

    // Make sure file exists
    boost::filesystem::path path = filepath;
    if (!boost::filesystem::exists(path)) {
        return res;
    }

    std::ifstream json(filepath, std::ifstream::binary);
    Json::Value root;
    Json::Reader *jsonparser = new Json::Reader;
    if (jsonparser->parse(json, root))
    {
        if (root.isMember("language"))
        {
            conf->iInstallerLanguage = root["language"].asUInt();
            res++;
        }
        if (root.isMember("platform"))
        {
            conf->iInstallerPlatform = root["platform"].asUInt();
            res++;
        }
        if (root.isMember("dlc"))
        {
            conf->bDLC = root["dlc"].asBool();
            res++;
        }
        if (root.isMember("ignore-dlc-count"))
        {
            conf->bIgnoreDLCCount = root["ignore-dlc-count"].asBool();
            res++;
        }
    }
    else
    {
        std::cout << "Failed to parse game specific config" << std::endl;
        std::cout << jsonparser->getFormattedErrorMessages() << std::endl;
    }
    delete jsonparser;
    if (json)
        json.close();

    return res;
}

int Util::replaceString(std::string& str, const std::string& to_replace, const std::string& replace_with)
{
    size_t pos = str.find(to_replace);
    if (pos == std::string::npos)
    {
        return 0;
    }
    str.replace(str.begin()+pos, str.begin()+pos+to_replace.length(), replace_with);
    return 1;
}

void Util::filepathReplaceReservedStrings(std::string& str, const std::string& gamename, const unsigned int& platformId, const std::string& dlcname)
{
    std::string platform;
    for (unsigned int i = 0; i < GlobalConstants::PLATFORMS.size(); ++i)
    {
        if ((platformId & GlobalConstants::PLATFORMS[i].id) == GlobalConstants::PLATFORMS[i].id)
        {
            platform = boost::algorithm::to_lower_copy(GlobalConstants::PLATFORMS[i].str);
            break;
        }
    }
    if (platform.empty())
    {
        if (str.find("%gamename%/%platform%") != std::string::npos)
            platform = "";
        else
            platform = "no_platform";
    }

    while (Util::replaceString(str, "%gamename%", gamename));
    while (Util::replaceString(str, "%dlcname%", dlcname));
    while (Util::replaceString(str, "%platform%", platform));
    while (Util::replaceString(str, "//", "/")); // Replace any double slashes with single slash
}

void Util::setFilePermissions(const boost::filesystem::path& path, const boost::filesystem::perms& permissions)
{
    if (boost::filesystem::exists(path))
    {
        if (boost::filesystem::is_regular_file(path))
        {
            boost::filesystem::file_status s = boost::filesystem::status(path);
            if (s.permissions() != permissions)
            {
                boost::system::error_code ec;
                boost::filesystem::permissions(path, permissions, ec);
                if (ec)
                {
                    std::cout << "Failed to set file permissions for " << path.string() << std::endl;
                }
            }
        }
    }
}

int Util::getTerminalWidth()
{
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return static_cast<int>(w.ws_col);
}


void Util::getDownloaderUrlsFromJSON(const Json::Value &root, std::vector<std::string> &urls)
{
    if(root.size() > 0) {
        for(Json::ValueIterator it = root.begin() ; it != root.end() ; ++it)
        {
            if (it.key() == "downloaderUrl")
            {
                Json::Value& url = *it;
                urls.push_back(url.asString());
            }
            else
                getDownloaderUrlsFromJSON(*it, urls);
        }
    }
    return;
}

std::vector<std::string> Util::getDLCNamesFromJSON(const Json::Value &root)
{
    std::vector<std::string> urls, dlcnames;
    getDownloaderUrlsFromJSON(root, urls);

    for (unsigned int i = 0; i < urls.size(); ++i)
    {
        std::string gamename;
        if (urls[i].find(GlobalConstants::PROTOCOL_PREFIX) == std::string::npos)
            continue;

        gamename.assign(urls[i].begin()+urls[i].find(GlobalConstants::PROTOCOL_PREFIX)+GlobalConstants::PROTOCOL_PREFIX.length(), urls[i].begin()+urls[i].find_last_of("/"));
        bool bDuplicate = false;
        for (unsigned int j = 0; j < dlcnames.size(); ++j)
        {
            if (gamename == dlcnames[j])
            {
                bDuplicate = true;
                break;
            }
        }
        if (!bDuplicate)
            dlcnames.push_back(gamename);
    }
    return dlcnames;
}

std::string Util::getHomeDir()
{
    return (std::string)getenv("HOME");
}

std::string Util::getConfigHome()
{
    std::string configHome;
    char *xdgconfig = getenv("XDG_CONFIG_HOME");
    if (xdgconfig)
        configHome = (std::string)xdgconfig;
    else
        configHome = Util::getHomeDir() + "/.config";
    return configHome;
}

std::string Util::getCacheHome()
{
    std::string cacheHome;
    char *xdgcache = getenv("XDG_CACHE_HOME");
    if (xdgcache)
        cacheHome = (std::string)xdgcache;
    else
        cacheHome = Util::getHomeDir() + "/.cache";
    return cacheHome;
}

std::vector<std::string> Util::tokenize(const std::string& str, const std::string& separator)
{
    std::vector<std::string> tokens;
    std::string token;
    size_t idx = 0, found;
    while ((found = str.find(separator, idx)) != std::string::npos)
    {
        token = str.substr(idx, found - idx);
        if (!token.empty())
            tokens.push_back(token);
        idx = found + separator.length();
    }
    token = str.substr(idx);
    if (!token.empty())
        tokens.push_back(token);

    return tokens;
}

unsigned int Util::getOptionValue(const std::string& str, const std::vector<GlobalConstants::optionsStruct>& options)
{
    unsigned int value = 0;
    boost::regex expression("^[+-]?\\d+$", boost::regex::perl);
    boost::match_results<std::string::const_iterator> what;
    if (str == "all")
    {
        value = (2 << (options.size() - 1)) - 1;
    }
    else if (boost::regex_search(str, what, expression))
    {
        value = std::stoi(str);
    }
    else
    {
        for (unsigned int i = 0; i < options.size(); ++i)
        {
            if (!options[i].regexp.empty())
            {
                boost::regex expr("^(" + options[i].regexp + ")$", boost::regex::perl | boost::regex::icase);
                if (boost::regex_search(str, what, expr))
                {
                    value = options[i].id;
                    break;
                }
            }
            else if (str == options[i].code)
            {
                value = options[i].id;
                break;
            }
        }
    }
    return value;
}
