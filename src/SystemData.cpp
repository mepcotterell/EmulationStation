#include "SystemData.h"
#include "GameData.h"
#include "XMLReader.h"
#include <boost/filesystem.hpp>
#include <fstream>
#include <stdlib.h>
#include <SDL/SDL_joystick.h>
#include "Renderer.h"
#include "AudioManager.h"
#include "Log.h"
#include "InputManager.h"
#include <iostream>

std::vector<SystemData*> SystemData::sSystemVector;

namespace fs = boost::filesystem;

extern bool PARSEGAMELISTONLY;
extern bool IGNOREGAMELIST;

std::string SystemData::getStartPath() { return mStartPath; }
std::string SystemData::getExtension() { return mSearchExtension; }

SystemData::SystemData(std::string name, std::string descName, std::string startPath, std::string extension, std::string command)
{
	mName = name;
	mDescName = descName;

	//expand home symbol if the startpath contains ~
	if(startPath[0] == '~')
	{
		startPath.erase(0, 1);
		std::string home = getenv("HOME");
		startPath.insert(0, home);
        }

	mStartPath = startPath;
	mSearchExtension = extension;
	mLaunchCommand = command;

	mRootFolder = new FolderData(this, mStartPath, "Search Root");

	if(!PARSEGAMELISTONLY)
		populateFolder(mRootFolder);

	if(!IGNOREGAMELIST)
		parseGamelist(this);

	mRootFolder->sort();
}

SystemData::~SystemData()
{
	delete mRootFolder;
}

std::string strreplace(std::string& str, std::string replace, std::string with)
{
	size_t pos = str.find(replace);

	if(pos != std::string::npos)
		return str.replace(pos, replace.length(), with.c_str(), with.length());
	else
		return str;
}

void SystemData::launchGame(Window* window, GameData* game)
{
	LOG(LogInfo) << "Attempting to launch game...";

	window->deinit();

	std::string command = mLaunchCommand;

	command = strreplace(command, "%ROM%", game->getBashPath());
	command = strreplace(command, "%BASENAME%", game->getBaseName());

	LOG(LogInfo) << "	" << command;
	std::cout << "==============================================\n";
	int exitCode = system(command.c_str());
	std::cout << "==============================================\n";

	if(exitCode != 0)
	{
		LOG(LogWarning) << "...launch terminated with nonzero exit code " << exitCode << "!";
	}

	window->init();
}

void SystemData::populateFolder(FolderData* folder)
{
	std::string folderPath = folder->getPath();
	if(!fs::is_directory(folderPath))
	{
		LOG(LogWarning) << "Error - folder with path \"" << folderPath << "\" is not a directory!";
		return;
	}

	//make sure that this isn't a symlink to a thing we already have
	if(fs::is_symlink(folderPath))
	{
		//if this symlink resolves to somewhere that's at the beginning of our path, it's gonna recurse
		if(folderPath.find(fs::canonical(folderPath).string()) == 0)
		{
			LOG(LogWarning) << "Skipping infinitely recursive symlink \"" << folderPath << "\"";
			return;
		}
	}

	for(fs::directory_iterator end, dir(folderPath); dir != end; ++dir)
	{
		fs::path filePath = (*dir).path();

		if(filePath.stem().string().empty())
			continue;

		if(fs::is_directory(filePath))
		{
			FolderData* newFolder = new FolderData(this, filePath.string(), filePath.stem().string());
			populateFolder(newFolder);

			//ignore folders that do not contain games
			if(newFolder->getFileCount() == 0)
				delete newFolder;
			else
				folder->pushFileData(newFolder);
		}else{
			//this is a little complicated because we allow a list of extensions to be defined (delimited with a space)
			//we first get the extension of the file itself:
			std::string extension = filePath.extension().string();
			std::string chkExt;
			size_t extPos = 0;

			do {
				//now we loop through every extension in the list
				size_t cpos = extPos;
				extPos = mSearchExtension.find(" ", extPos);
				chkExt = mSearchExtension.substr(cpos, ((extPos == std::string::npos) ? mSearchExtension.length() - cpos: extPos - cpos));

				//if it matches, add it
				if(chkExt == extension)
				{
					GameData* newGame = new GameData(this, filePath.string(), filePath.stem().string());
					folder->pushFileData(newGame);
					break;
				}else if(extPos != std::string::npos) //if not, add one to the "next position" marker to skip the space when reading the next extension
				{
					extPos++;
				}

			} while(extPos != std::string::npos && chkExt != "" && chkExt.find(".") != std::string::npos);
		}
	}
}

std::string SystemData::getName()
{
	return mName;
}

std::string SystemData::getDescName()
{
	return mDescName;
}

//creates systems from information located in a config file
void SystemData::loadConfig()
{
	deleteSystems();

	std::string path = getConfigPath();

	LOG(LogInfo) << "Loading system config file...";

	std::ifstream file(path.c_str());
	if(file.is_open())
	{
		std::string line;
		std::string sysName, sysDescName, sysPath, sysExtension, sysCommand;
		while(file.good())
		{
			std::getline(file, line);

			//skip blank lines and comments
			if(line.empty() || line[0] == *"#")
				continue;

			//find the name (left of the equals sign) and the value (right of the equals sign)
			bool lineValid = false;
			std::string varName, varValue;
			for(unsigned int i = 0; i < line.length(); i++)
			{
				if(line[i] == *"=")
				{
					lineValid = true;
					varName = line.substr(0, i);
					varValue = line.substr(i + 1, line.length() - 1);
					break;
				}
			}

			if(lineValid)
			{
				//map the value to the appropriate variable
				if(varName == "NAME")
					sysName = varValue;
				else if(varName == "DESCNAME")
					sysDescName = varValue;
				else if(varName == "PATH")
				{
					if(varValue[varValue.length() - 1] == '/')
						sysPath = varValue.substr(0, varValue.length() - 1);
					else
						sysPath = varValue;
				}else if(varName == "EXTENSION")
					sysExtension = varValue;
				else if(varName == "COMMAND")
					sysCommand = varValue;

				//we have all our variables - create the system object
				if(!sysName.empty() && !sysPath.empty() &&!sysExtension.empty() && !sysCommand.empty())
				{
					if(sysDescName.empty())
						sysDescName = sysName;

					SystemData* newSystem = new SystemData(sysName, sysDescName, sysPath, sysExtension, sysCommand);
					if(newSystem->getRootFolder()->getFileCount() == 0)
					{
						LOG(LogWarning) << "System \"" << sysName << "\" has no games! Ignoring it.";
						delete newSystem;
					}else{
						sSystemVector.push_back(newSystem);
					}

					//reset the variables for the next block (should there be one)
					sysName = ""; sysDescName = ""; sysPath = ""; sysExtension = ""; sysCommand = "" ;
				}
			}else{
				LOG(LogError) << "Error reading config file \"" << path << "\" - no equals sign found on line \"" << line << "\"!";
				return;
			}
		}
	}else{
		LOG(LogError) << "Error - could not load config file \"" << path << "\"!";
		return;
	}

	LOG(LogInfo) << "Finished loading config file - created " << sSystemVector.size() << " systems.";
	return;
}

void SystemData::writeExampleConfig()
{
	std::string path = getConfigPath();

	std::ofstream file(path.c_str());

	file << "# This is the EmulationStation Systems configuration file." << std::endl;
	file << "# Lines that begin with a hash (#) are ignored, as are empty lines." << std::endl;
	file << "# A sample system might look like this:" << std::endl;
	file << "#NAME=nes" << std::endl;
	file << "#DESCNAME=Nintendo Entertainment System" << std::endl;
	file << "#PATH=~/ROMs/nes/" << std::endl;
	file << "#EXTENSION=.nes .NES" << std::endl;
	file << "#COMMAND=retroarch -L ~/cores/libretro-fceumm.so %ROM%" << std::endl << std::endl;

	file << "#NAME is a short name used internally (and in alternative paths)." << std::endl;
	file << "#DESCNAME is a descriptive name to identify the system. It may be displayed in a header." << std::endl;
	file << "#PATH is the path to start the recursive search for ROMs in. ~ will be expanded into the $HOME variable." << std::endl;
	file << "#EXTENSION is a list of extensions to search for, separated by spaces. You MUST include the period, and it must be exact - it's case sensitive, and no wildcards." << std::endl;
	file << "#COMMAND is the shell command to execute when a game is selected. %ROM% will be replaced with the (bash special-character escaped) path to the ROM." << std::endl << std::endl;

	file << "#Now try your own!" << std::endl;
	file << "NAME=" << std::endl;
	file << "DESCNAME=" << std::endl;
	file << "PATH=" << std::endl;
	file << "EXTENSION=" << std::endl;
	file << "COMMAND=" << std::endl;

	file.close();
}

void SystemData::deleteSystems()
{
	for(unsigned int i = 0; i < sSystemVector.size(); i++)
	{
		delete sSystemVector.at(i);
	}
	sSystemVector.clear();
}

std::string SystemData::getConfigPath()
{
	std::string home = getenv("HOME");
	if(home.empty())
	{
		LOG(LogError) << "$HOME environment variable empty or nonexistant!";
		exit(1);
		return "";
	}

	return(home + "/.emulationstation/es_systems.cfg");
}

FolderData* SystemData::getRootFolder()
{
	return mRootFolder;
}

std::string SystemData::getGamelistPath(){

	std::string filePath;

	filePath = mRootFolder->getPath() + "/gamelist.xml";
	if(fs::exists(filePath))
		return filePath;

	filePath = getenv("HOME");
	filePath += "/.emulationstation/"+ getName() + "/gamelist.xml";
	if(fs::exists(filePath))
		return filePath;

	return "";
}

bool SystemData::hasGamelist()
{
	if(getGamelistPath().empty())
		return false;
	else
		return true;
}
