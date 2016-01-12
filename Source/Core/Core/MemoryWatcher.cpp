// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

#include "Common/FileUtil.h"
#include "Common/Thread.h"
#include "Core/MemoryWatcher.h"
#include "Core/HW/Memmap.h"

// We don't want to kill the cpu, so sleep for this long after polling.
static const int SLEEP_DURATION = 2; // ms

MemoryWatcher::MemoryWatcher()
{
	if (!LoadAddresses(File::GetUserPath(F_MEMORYWATCHERLOCATIONS_IDX)))
		return;
	if (!OpenSocket(File::GetUserPath(F_MEMORYWATCHERSOCKET_IDX)))
		return;
	m_pipe = open(File::GetUserPath(F_MEMORYWATCHERPIPE_IDX).c_str(), O_RDONLY | O_NONBLOCK);
	m_running = true;
	m_watcher_thread = std::thread(&MemoryWatcher::WatcherThread, this);
}

MemoryWatcher::~MemoryWatcher()
{
	if (!m_running)
		return;

	if (m_fd >= 0)
	{
		close(m_fd);
	}
	m_running = false;
	m_watcher_thread.join();
}

bool MemoryWatcher::LoadAddresses(const std::string& path)
{
	std::ifstream locations(path);
	if (!locations)
		return false;

	std::string line;
	while (std::getline(locations, line))
	{
		m_fileAddresses.push_back(Address(line));
	}

	return m_fileAddresses.size() > 0;
}

MemoryWatcher::Address::Address(const std::string& line)
{
	value = 0;

	bits = X32;
	offsets = std::vector<u32>();
	bool aliasSet = false;

	std::stringstream ss(line);
    if(line.find(':') != std::string::npos)
    {
    	std::string a;
    	std::getline(ss, a, ':');
    	alias = a;
    	aliasSet = true;
    }
    if(ss.str().substr(ss.tellg()).find("bits") != std::string::npos)
    {
    	std::string b;
    	std::getline(ss, b, 'b');
    	int startPos = b.find_first_not_of(' ');
    	int endPos = b.find_first_of('b');
    	b = b.substr(startPos, endPos - startPos);
    	if(b == "8") bits = X8;
	    if(b == "16") bits = X16;

    	std::string temp;
    	ss >> temp;
    	ss.get();
    }
    if(!aliasSet)
    {
    	alias = ss.str().substr(ss.tellg());
    }
	ss >> std::hex;
	u32 offset;
	while (ss >> offset)
		offsets.push_back(offset);
}

bool MemoryWatcher::OpenSocket(const std::string& path)
{
	memset(&m_addr, 0, sizeof(m_addr));
	m_addr.sun_family = AF_UNIX;
	strncpy(m_addr.sun_path, path.c_str(), sizeof(m_addr.sun_path) - 1);

	m_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	return m_fd >= 0;
}

bool MemoryWatcher::Address::Read()
{
	u32 currentAddress = 0;
	int length = offsets.size();
	for (int i = 0; i < length; i++)
	{
		if(i == length - 1)
		{
			u32 oldValue = value;
			switch(bits)
			{
			case(X8): value = Memory::Read_U16(currentAddress + offsets[i]) & 0xFF;
				break;
			case(X16): value = Memory::Read_U16(currentAddress + offsets[i]);
				break;
			default: value = Memory::Read_U32(currentAddress + offsets[i]);
			}
			return (oldValue != value);
		}
		else
		{
			currentAddress = Memory::Read_U32(currentAddress + offsets[i]);
		}
	}
	return false;
}

std::string MemoryWatcher::Address::ComposeMessage()
{
	std::stringstream message_stream;
	message_stream << alias << '\n' << std::hex << value;
	return message_stream.str();
}

void MemoryWatcher::WatcherThread()
{
	while (m_running)
	{

		for (Address& address : m_fileAddresses)
		{
			if (address.Read())
			{
				std::string message = address.ComposeMessage();
				sendto(
					m_fd,
					message.c_str(),
					message.size() + 1,
					0,
					reinterpret_cast<sockaddr*>(&m_addr),
					sizeof(m_addr));
			}
		}
		Common::SleepCurrentThread(SLEEP_DURATION);
	}
}
