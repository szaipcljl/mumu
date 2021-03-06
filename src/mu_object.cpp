#include "mu_object.h"
#include "mu_object_p.h"
#include "mu_method.h"
#include "mu_warn.h"
#include "mu_thread_data.h"
#include <windows.h>


static int index_of_method(mu_metaobject** pMetaObj, mu_method_ptr pMethodSignal);

static int match_method(const mu_metaobject* pMetaObj, mu_method_ptr pMethodSignal);

static int method_count(const mu_metaobject* pMetaObj);

static int count_of_signal(const mu_metaobject* pMetaObj);

static int relative_count_of_signal(const mu_metaobject* pMetaObj);

static int relative_count_of_method(const mu_metaobject* pMetaObj);

void mu_object::connect(mu_object *sender, const char *signal,
	const mu_object *receiver, const char *method, ConnectionType type /*= ConnectionType::AutoConnection*/)
{
	if (!sender)
	{
		mu_warn::out_put("connect error: Params is NULL.\n");
		return;
	}
	sender->connect(signal, receiver, method, type);
}

void mu_object::connect(const char *signal, const mu_object *receiver,
	const char *method, ConnectionType type /*= ConnectionType::AutoConnection*/)
{
	if (!signal || !receiver || !method)
	{
		mu_warn::out_put("connect error: Params is NULL.\n");
		return;
	}
	if (signal[0] != '2')
	{
		mu_warn::out_put("connect error: Slot cannot send sig.\n");
		return;
	}
	mu_method_ptr pSignal(new mu_method(&signal[1], mu_method::SIGNAL_METHOD));
	mu_method_ptr pSlot(new mu_method(&method[1], mu_method::SLOT_METHOD));
	mu_metaobject* smeta = const_cast<mu_metaobject*>(metaObject());
	int signal_relative_index = index_of_method(&smeta, pSignal);

	if (signal_relative_index == -1)
	{
		mu_warn::out_put("connect error: Have no such signal.\n");
		return;
	}
	mu_metaobject* rmeta = const_cast<mu_metaobject*>(receiver->metaObject());

	int slot_relative_index = index_of_method(&rmeta, pSlot);

	if (slot_relative_index == -1)
	{
		mu_warn::out_put("connect error: Have no such slot.\n");
		return;
	}
	mu_object_p* pMuObjP = mu_object_p::get(pPrivate);
	mu_object_p* pMuRObj = mu_object_p::get(const_cast<mu_object*>(receiver)->get_p());

	mu_connect_ptr pConnect(new mu_connect(rmeta,
		const_cast<mu_object*>(receiver), slot_relative_index + relative_count_of_method(rmeta)));
	pConnect->set_connect_type(type);

	pMuObjP->add_connect(signal_relative_index + relative_count_of_signal(smeta), pConnect);

	mu_connect_ptr pSenderConnect(new mu_connect(smeta, this, signal_relative_index + relative_count_of_signal(smeta)));
	pMuRObj->add_sender(pSenderConnect);
}

mu_object::~mu_object()
{
	MU_EMIT destroyed(this);
	mu_object_p* p = mu_object_p::get(pPrivate);
	if(!p->is_in_signal())
	{
		mu_thread_data* pData = p->get_thread_data();
		pData->decrease_ref();
		delete p;
	}
	else
	{
		p->set_delete_later(true);
	}
	
}

mu_object::mu_object()
{
	mu_object_p* p = new mu_object_p(this);
	pPrivate = reinterpret_cast<void*>(p);
	mu_thread_data* pData = mu_thread_data::get_current();
	pData->add_ref();
	p->set_thread_data(pData);
}

void* mu_object::get_p()
{
	return pPrivate;
}

void mu_object::set_dispatcher(mu_event_dispatcher* pDispatcher)
{
	mu_thread_data* pData = mu_thread_data::get_current();
	pData->set_event_dispatcher(pDispatcher);
}


void mu_metaobject::activate(mu_object *sender, int signal_index, void **argv)
{

}

void mu_metaobject::activate(mu_object *sender, const mu_metaobject *m, int local_signal_index, void **argv)
{
	int signal_relative_index = local_signal_index + relative_count_of_signal(m);
	mu_object_p* pMuObjP = mu_object_p::get(sender->get_p());
	if (!pMuObjP->is_connected(signal_relative_index))
	{
		mu_warn::out_put("send error: The signal has no connected.\n");
		return;
	}
	pMuObjP->begin_connect_list();
	mu_connect_list_ptr pList = pMuObjP->get_connect_list(signal_relative_index);
	for (auto it = pList->begin(); it != pList->end(); )
	{
		auto itNext = it;
		++itNext;
		mu_object* receiver = (*it)->get_obj();
		mu_object_p* pMuObjR = mu_object_p::get(receiver->get_p());
		mu_thread_data* pThreadData = pMuObjR->get_thread_data();
		DWORD currentId = GetCurrentThreadId();
		if ((currentId != pThreadData->get_thread_id()
			&& (*it)->get_connect_type() == ConnectionType::AutoConnection)
			|| (*it)->get_connect_type() == ConnectionType::QueuedConnection)
		{
			mu_metaobject* rmeta = (*it)->get_metaobj();
			int method_index = (*it)->get_method_index() - relative_count_of_method(rmeta);
			mu_type* pType = rmeta->d.static_metatype(method_index, argv);
			if (!pType)
			{
				mu_warn::out_put("send error:Cannot Find Mu_Type.\n");
			}
			else
			{
				mu_event* pEvent = new mu_event;
				pEvent->set_metaobj(rmeta);
				pEvent->set_type(pType);
				pEvent->set_method_index(method_index);
				pEvent->set_receiver(receiver);
				mu_event_dispatcher* pDisptcher = pThreadData->get_event_dispatcher();
				if (!pDisptcher)
				{
					mu_warn::out_put("send error:Please SetUp Dispatcher for current thread.\n");
				}
				else
				{
					pDisptcher->post_event(pEvent);
				}
			}
		}
		else if ((*it)->get_connect_type() == ConnectionType::DirectConnection
			|| (*it)->get_connect_type() == ConnectionType::AutoConnection)
		{
			mu_metaobject* rmeta = (*it)->get_metaobj();
			int method_index = (*it)->get_method_index() - relative_count_of_method(rmeta);
			rmeta->d.static_metacall(receiver, mu_metaobject::InvokeMetaMethod, method_index, argv);
		}
		it = itNext;
	}
	if(!pMuObjP->check_delete_later())
	{
		pMuObjP->end_connect_list();
	}
}

void mu_metaobject::activate(mu_object *sender, int signal_offset, int local_signal_index, void **argv)
{

}

int index_of_method(mu_metaobject** pMetaObj, mu_method_ptr pMethodSignal)
{
	const mu_metaobject* m = *pMetaObj;
	int i = 0;
	for (; m; m = m->d.superdata)
	{
		i = match_method(m, pMethodSignal);
		if (i >= 0)
		{
			*pMetaObj = const_cast<mu_metaobject*>(m);
			return i;
		}
	}
	return -1;
}

int match_method(const mu_metaobject* pMetaObj, mu_method_ptr pMethodSignal)
{

	int nEnd = pMethodSignal->get_method_type() == mu_method::SIGNAL_METHOD ?
		count_of_signal(pMetaObj) : method_count(pMetaObj);
	int i = pMethodSignal->get_method_type() == mu_method::SIGNAL_METHOD ? 0 : count_of_signal(pMetaObj);
	const unsigned int* pData = &pMetaObj->d.data[2];
	for (int j = 0; j < i; ++j)
	{
		pData = pData + 2 + pData[1];
	}
	for (; i < nEnd; ++i)
	{
		if (pMetaObj->d.stringdata[pData[0]] != pMethodSignal->get_name())
		{
			pData += (2 + pData[1]);
			continue;
		}
		if (pMethodSignal->get_type_count() != pData[1])
		{
			pData += (2 + pData[1]);
			continue;
		}
		unsigned int a;
		for (a = 0; a < pData[1]; ++a)
		{
			if (pMetaObj->d.stringdata[pData[a + 2]] != pMethodSignal->get_type(a))
			{
				pData += (2 + pData[1]);
				break;
			}
		}
		if (a != pData[1])
		{
			continue;
		}
		return i;
	}
	return -1;
}

int method_count(const mu_metaobject* pMetaObj)
{
	return pMetaObj->d.data[0];
}

int count_of_signal(const mu_metaobject* pMetaObj)
{
	return pMetaObj->d.data[1];

}

int relative_count_of_signal(const mu_metaobject* pMetaObj)
{
	int nRetCount = 0;
	const mu_metaobject* m = pMetaObj->d.superdata;
	for (; m; m = m->d.superdata)
	{
		nRetCount += count_of_signal(m);
	}
	return nRetCount;
}

int relative_count_of_method(const mu_metaobject* pMetaObj)
{
	int nRetCount = 0;
	const mu_metaobject* m = pMetaObj->d.superdata;
	for (; m; m = m->d.superdata)
	{
		nRetCount += method_count(m);
	}
	return nRetCount;
}

void mu_event_dispatcher::handle_event(mu_event* pEvent)
{
	mu_metaobject* rmeta = pEvent->get_metaobj();
	int method_index = pEvent->get_method_index();
	mu_type* pType = pEvent->get_type();
	void* argv[] = { 0, reinterpret_cast<void*>(pType) };
	rmeta->d.static_metacall(pEvent->get_receiver(), mu_metaobject::AsyncInvokeMetaMethod, method_index, argv);
	delete pEvent;
	delete pType;
}
