


#include "Message.h"
#include "FakeMessenger.h"
#include "mds/MDS.h"
#include "include/LogType.h"
#include "include/Logger.h"

#include "include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <ext/hash_map>
#include <cassert>
#include <iostream>

using namespace std;


#include "Cond.h"
#include "Mutex.h"
#include <pthread.h>

#include "include/config.h"


// global queue.

map<int, FakeMessenger*> directory;
hash_map<int, Logger*>        loggers;
LogType fakemsg_logtype;

Mutex lock;
Cond  cond;

bool      awake = false;
bool      shutdown = false;
pthread_t thread_id;

void *fakemessenger_thread(void *ptr) 
{
  dout(1) << "thread start" << endl;

  lock.Lock();
  while (1) {
	dout(1) << "thread waiting" << endl;
	awake = false;
	cond.Wait(lock);
	awake = true;
	dout(1) << "thread woke up" << endl;
	if (shutdown) break;

	lock.Unlock();
	fakemessenger_do_loop();
	lock.Lock();
  }
  lock.Unlock();

  dout(1) << "thread finish (i woke up but no messages, bye)" << endl;
}


void fakemessenger_startthread() {
  pthread_create(&thread_id, NULL, fakemessenger_thread, 0);
}

void fakemessenger_stopthread() {
  cout << "fakemessenger_stopthread setting stop flag" << endl;
  lock.Lock();  
  shutdown = true;
  lock.Unlock();
  cond.Signal();

  cout << "fakemessenger_stopthread waiting" << endl;
  void *ptr;
  pthread_join(thread_id, &ptr);
}




// lame main looper

int fakemessenger_do_loop()
{
  lock.Lock();
  dout(1) << "do_loop begin." << endl;

  while (1) {
	bool didone = false;
	
	dout(11) << "do_loop top" << endl;

	map<int, FakeMessenger*>::iterator it = directory.begin();
	while (it != directory.end()) {
	  Message *m = it->second->get_message();
	  if (m) {
		dout(15) << "got " << m << endl;
		dout(3) << "---- do_loop dispatching '" << m->get_type_name() << 
		  "' from " << MSG_ADDR_NICE(m->get_source()) << ':' << m->get_source_port() <<
		  " to " << MSG_ADDR_NICE(m->get_dest()) << ':' << m->get_dest_port() << " ---- " << m 
			 << endl;
		
		if (g_conf.fakemessenger_serialize) {
		  int t = m->get_type();
		  if (true
			  || t == MSG_CLIENT_REQUEST
			  || t == MSG_CLIENT_REPLY
			  || t == MSG_MDS_DISCOVER
			  ) {
			// serialize
			crope buffer;
			m->encode(buffer);
			delete m;
			
			// decode
			m = decode_message(buffer);
			assert(m);
		  }
		}
		
		didone = true;

		lock.Unlock();
		it->second->dispatch(m);
		lock.Lock();
	  }
	  it++;
	}
	
	
	if (!didone)
	  break;
  }

  dout(1) << "do_loop end (no more messages)." << endl;
  lock.Unlock();
  return 0;
}


// class

FakeMessenger::FakeMessenger(long me)
{
  whoami = me;
  directory[ whoami ] = this;

  cout << "fakemessenger " << whoami << " messenger is " << this << endl;

  string name;
  name = "m.";
  name += MSG_ADDR_TYPE(whoami);
  int w = MSG_ADDR_NUM(whoami);
  if (w >= 1000) name += ('0' + ((w/1000)%10));
  if (w >= 100) name += ('0' + ((w/100)%10));
  if (w >= 10) name += ('0' + ((w/10)%10));
  name += ('0' + ((w/1)%10));

  logger = new Logger(name, (LogType*)&fakemsg_logtype);
  loggers[ whoami ] = logger;
}

FakeMessenger::~FakeMessenger()
{
  shutdown();

  delete logger;
}


int FakeMessenger::shutdown()
{
  directory.erase(whoami);
}


int FakeMessenger::send_message(Message *m, msg_addr_t dest, int port, int fromport)
{
  m->set_source(whoami, fromport);
  m->set_dest(dest, port);

  lock.Lock();

  // deliver
  try {
#ifdef LOG_MESSAGES
	// stats
	loggers[whoami]->inc("+send",1);
	loggers[dest]->inc("-recv",1);

	char s[20];
	sprintf(s,"+%s", m->get_type_name());
	loggers[whoami]->inc(s);
	sprintf(s,"-%s", m->get_type_name());
	loggers[dest]->inc(s);
#endif

	// queue
	FakeMessenger *dm = directory[dest];
	dm->queue_incoming(m);

	dout(10) << "sending " << m << " to " << dest << endl;
	
  }
  catch (...) {
	cout << "no destination " << dest << endl;
	assert(0);
  }


  // wake up loop?
  if (!awake) {
	dout(1) << "waking up fakemessenger thread" << endl; 
	awake = true;
	lock.Unlock();
	cond.Signal();
  } else
	lock.Unlock();


}


