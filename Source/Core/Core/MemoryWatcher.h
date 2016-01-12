// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <map>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <sys/un.h>

// MemoryWatcher reads a file containing in-game memory addresses and outputs
// changes to those memory addresses to a unix domain socket as the game runs.
//
// The input file is a newline-separated list of hex memory addresses, without
// the "0x". To follow pointers, separate addresses with a space. For example,
// "ABCD EF" will watch the address at (*0xABCD) + 0xEF.
// The output to the socket is two lines. The first is the address from the
// input file, and the second is the new value in hex.
enum
{
	X8,
	X16,
	X32,
};

class MemoryWatcher final
{
public:
	MemoryWatcher();
	~MemoryWatcher();

private:

	class Address
	{
		public:
			Address(const std::string& line);
			bool Read();
			std::string ComposeMessage();

			std::vector<u32> offsets;
			std::string alias;
			int bits;
			u32 value;
	};

	bool LoadAddresses(const std::string& path);
	bool OpenSocket(const std::string& path);

	void ParseLine(const std::string& line);

	void WatcherThread();

	std::thread m_watcher_thread;
	std::atomic_bool m_running{false};

	int m_fd = -1;
	int m_pipe = -1;
	sockaddr_un m_addr;

	// // Address as stored in the file -> list of offsets to follow
	// std::map<std::string, std::vector<u32>> m_addresses;
	// Address as stored in the file -> current value
	std::vector<Address> m_fileAddresses;
	// Address as requested by the pipe -> current value
	// std::map<std::string, Address> m_pipeAddresses;
};
