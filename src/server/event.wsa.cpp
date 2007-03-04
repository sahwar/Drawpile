/*******************************************************************************

   Copyright (C) 2006, 2007 M.K.A. <wyrmchild@users.sourceforge.net>
   
   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   Except as contained in this notice, the name(s) of the above copyright
   holders shall not be used in advertising or otherwise to promote the sale,
   use or other dealings in this Software without prior writeitten authorization.
   
   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.

*******************************************************************************/

//#include "../shared/templates.h"
#include "event.h"

#ifndef NDEBUG
	#include <iostream>
	#include <ios>
#endif

#include <cerrno> // errno
#include <memory> // memcpy()
#include <cassert> // assert()

const uint32_t
	//! identifier for 'read' event
	Event::read = FD_READ,
	//! identifier for 'write' event
	Event::write = FD_WRITE,
	//! error event
	Event::error = 0xFFFF,
	//! hangup event
	Event::hangup = FD_CLOSE;

Event::Event() throw()
{
	#if defined(DEBUG_EVENTS) and !defined(NDEBUG)
	std::cout << "Event(wsa)()" << std::endl;
	std::cout << "Max events: " << max_events<< std::endl;
	#endif
	
	// null the ev set
	memset(w_ev, 0, max_events);
}

Event::~Event() throw()
{
	#if defined(DEBUG_EVENTS) and !defined(NDEBUG)
	std::cout << "~Event(wsa)()" << std::endl;
	#endif
}

bool Event::init() throw()
{
	#if defined(DEBUG_EVENTS) and !defined(NDEBUG)
	std::cout << "Event(wsa).init()" << std::endl;
	#endif
	
	return true;
}

void Event::finish() throw()
{
	#if defined(DEBUG_EVENTS) and !defined(NDEBUG)
	std::cout << "Event::finish()" << std::endl;
	#endif
}

int Event::wait() throw()
{
	#if defined(DEBUG_EVENTS) and !defined(NDEBUG)
	std::cout << "Event(wsa).wait(msecs: " << msecs << ")" << std::endl;
	#endif
	
	assert(fd_to_ev.size() != 0);
	
	nfds = WSAWaitForMultipleEvents(fd_to_ev.size(), w_ev, 0, _timeout, true);
	if (nfds == WSA_WAIT_FAILED)
	{
		_error = WSAGetLastError();
		switch (_error)
		{
		#ifndef NDEBUG
		case WSA_INVALID_HANDLE:
			assert(!(_error == WSA_INVALID_HANDLE));
			break;
		case WSANOTINITIALISED:
			assert(!(_error == WSANOTINITIALISED));
			break;
		case WSA_INVALID_PARAMETER:
			assert(!(_error == WSA_INVALID_PARAMETER));
			break;
		#endif
		case WSAENETDOWN:
		case WSAEINPROGRESS:
		case WSA_NOT_ENOUGH_MEMORY:
			break;
		default:
			std::cerr << "Event(wsa).wait() - unknown error: " << nfds << std::endl;
			break;
		}
		
		return -1;
	}
	else
	{
		switch (nfds)
		{
		case WSA_WAIT_IO_COMPLETION:
		case WSA_WAIT_TIMEOUT:
			return 0;
		default:
			// events ready
			break;
		}
	}
	
	return fd_to_ev.size();
}

int Event::add(fd_t fd, uint32_t ev) throw()
{
	#if defined(DEBUG_EVENTS) and !defined(NDEBUG)
	std::cout << "Event(wsa).add(fd: " << fd << ", event: ";
	std::cout.setf ( std::ios_base::hex, std::ios_base::basefield );
	std::cout.setf ( std::ios_base::showbase );
	std::cout << ev;
	std::cout.setf ( std::ios_base::dec );
	std::cout.setf ( ~std::ios_base::showbase );
	std::cout << ")" << std::endl;
	#endif
	
	assert( ev == read or ev == write or ev == read|write );
	assert( fd >= 0 );
	
	// hack for WSA
	if (fIsSet(ev, read));
		fSet(ev, static_cast<uint32_t>(FD_ACCEPT));
	if (fIsSet(ev, read) or fIsSet(ev, write));
		fSet(ev, static_cast<uint32_t>(FD_CLOSE|FD_CONNECT));
	
	for (uint32_t i=0; i != max_events; i++)
	{
		if (w_ev[i] == 0)
		{
			w_ev[i] = WSACreateEvent();
			if (!(w_ev[i])) return false;
			
			WSAEventSelect(fd, w_ev[i], ev);
			
			fd_to_ev[fd] = i;
			ev_to_fd[i] = fd;
			
			return true;
		}
	}
	
	return false;
}

int Event::modify(fd_t fd, uint32_t ev) throw()
{
	#if defined(DEBUG_EVENTS) and !defined(NDEBUG)
	std::cout << "Event(wsa).modify(fd: " << fd << ", event: ";
	std::cout.setf ( std::ios_base::hex, std::ios_base::basefield );
	std::cout.setf ( std::ios_base::showbase );
	std::cout << ev;
	std::cout.setf ( std::ios_base::dec );
	std::cout.setf ( ~std::ios_base::showbase );
	std::cout << ")" << std::endl;
	#endif
	
	assert( ev == read or ev == write or ev == read|write );
	assert( fd >= 0 );
	
	std::map<fd_t, uint32_t>::iterator fi(fd_to_ev.find(fd));
	if (fi == fd_to_ev.end())
		return false;
	
	WSAEventSelect(fd, w_ev[fi->second], ev);
	
	return 0;
}

int Event::remove(fd_t fd, uint32_t ev) throw()
{
	#if defined(DEBUG_EVENTS) and !defined(NDEBUG)
	std::cout << "Event(wsa).remove(fd: " << fd << ", event: ";
	std::cout.setf ( std::ios_base::hex, std::ios_base::basefield );
	std::cout.setf ( std::ios_base::showbase );
	std::cout << ev;
	std::cout.setf ( std::ios_base::dec );
	std::cout.setf ( ~std::ios_base::showbase );
	std::cout << ")" << std::endl;
	#endif
	
	assert( ev == read or ev == write or ev == read|write );
	assert( fd >= 0 );
	
	std::map<fd_t, uint32_t>::iterator fi(fd_to_ev.find(fd));
	if (fi == fd_to_ev.end())
		return false;
	
	WSAEventSelect(fd, w_ev[fi->second], 0);
	WSACloseEvent(w_ev[fi->second]);
	
	w_ev[fi->second] = 0;
	
	ev_to_fd.erase(fi->second);
	fd_to_ev.erase(fd);
	
	return true;
}

std::pair<fd_t, uint32_t> Event::getEvent() throw()
{
	#if defined(DEBUG_EVENTS) and !defined(NDEBUG)
	std::cout << "Event(wsa).getEvent()" << std::endl;
	#endif
	
	uint32_t get_event = nfds - WSA_WAIT_EVENT_0;
	
	uint32_t evs;
	std::map<uint32_t, fd_t>::iterator ev_to_fd_iter;
	for (; get_event != max_events; get_event++, nfds++)
	{
		if (w_ev[get_event])
		{
			ev_to_fd_iter = ev_to_fd.find(get_event);
			if (ev_to_fd_iter == ev_to_fd.end())
				break;
			
			if ((evs = getEvents(ev_to_fd_iter->second)) != 0)
			{
				nfds++;
				std::cout << "fd #" << ev_to_fd_iter->second << " was triggered!" << std::endl;
				return std::make_pair(ev_to_fd_iter->second, evs);
			}
			else
			{
				std::cout << "fd #" << ev_to_fd_iter->second << " was NOT triggered!" << std::endl;
			}
		}
	}
	
	return std::make_pair(static_cast<fd_t>(0), static_cast<uint32_t>(0));
}

uint32_t Event::getEvents(fd_t fd) const throw()
{
	#if defined(DEBUG_EVENTS) and !defined(NDEBUG)
	std::cout << "Event(wsa).getEvents(fd: " << fd << ")" << std::endl;
	#endif
	
	std::map<fd_t, uint32_t>::const_iterator fev(fd_to_ev.find(fd));
	if (fev == fd_to_ev.end())
		return 0;
	
	LPWSANETWORKEVENTS set(0);
	
	int r = WSAEnumNetworkEvents(fd, w_ev[fev->second], set);
	
	if (r == SOCKET_ERROR)
	{
		//_error = WSAGetLastError();
		
		switch (WSAGetLastError())
		{
		#ifndef NDEBUG
		case WSAEFAULT:
			assert(!(_error == WSAEFAULT));
			break;
		case WSANOTINITIALISED:
			assert(!(_error == WSANOTINITIALISED));
			break;
		case WSAEINVAL:
			assert(!(_error == WSAEINVAL));
			break;
		case WSAENOTSOCK:
			assert(!(_error == WSAEFAULT));
			break;
		case WSAEAFNOSUPPORT:
			assert(!(_error == WSAEAFNOSUPPORT));
			break;
		#endif
		case WSAECONNRESET: // reset by remote
		case WSAECONNABORTED: // connection aborted
		case WSAETIMEDOUT: // connection timed-out
		case WSAENETUNREACH: // network unreachable
		case WSAECONNREFUSED: // connection refused/rejected
			return FD_CLOSE;
		case WSAENOBUFS: // out of network buffers
		case WSAENETDOWN: // network sub-system failure
		case WSAEINPROGRESS: // something's in progress
			return 0;
		default:
			std::cerr << "Event(wsa).getEvents() - unknown error: " << _error << std::endl;
			break;
		}
		return 0;
	}
	
	// "Hack" for WSA's FD_ACCEPT
	if (fIsSet(set->lNetworkEvents, static_cast<long int>(FD_ACCEPT)));
		fSet(set->lNetworkEvents, static_cast<long int>(FD_READ));
	
	if (fIsSet(set->lNetworkEvents, static_cast<long int>(FD_CONNECT)))
		fSet(set->lNetworkEvents, static_cast<long int>(FD_WRITE));
	if (fIsSet(set->lNetworkEvents, static_cast<long int>(FD_CLOSE)))
		fSet(set->lNetworkEvents, static_cast<long int>(FD_WRITE|FD_READ));
	
	// enum already resets the event..
	// WSAResetEvent(w_ev[fev->second]);
	
	return set->lNetworkEvents;
}
