#pragma once

#include <chrono>
#include <filesystem>

#include "cpp-sdk/types/MValue.h"
#include "cpp-sdk/IResource.h"
#include "cpp-sdk/objects/IBaseObject.h"

#include "V8Entity.h"
#include "V8Timer.h"
#include "PromiseRejections.h"

class V8ResourceImpl : public alt::IResource::Impl
{
public:
	class FunctionImpl : public alt::IMValueFunction::Impl
	{
	public:
		FunctionImpl(V8ResourceImpl *_resource, v8::Local<v8::Function> fn) : resource(_resource),
																			  function(_resource->GetIsolate(), fn)
		{
		}

		alt::MValue Call(alt::MValueArgs args) const override;

	private:
		V8ResourceImpl *resource;
		v8::UniquePersistent<v8::Function> function;
	};

	V8ResourceImpl(v8::Isolate *_isolate, alt::IResource *_resource) : isolate(_isolate),
																	   resource(_resource)
	{

	}

	~V8ResourceImpl();

	struct PathInfo
	{
		alt::IPackage *pkg = nullptr;
		std::string fileName;
		std::string prefix;
	};

	bool Start() override;

	void OnTick() override;

	inline alt::IResource *GetResource() { return resource; }
	inline v8::Isolate *GetIsolate() { return isolate; }
	inline v8::Local<v8::Context> GetContext() { return context.Get(isolate); }

	void SubscribeLocal(const std::string &ev, v8::Local<v8::Function> cb, V8::SourceLocation &&location)
	{
		if (ev.empty())
			localHandlers.insert({ ev, V8::GenericEventCallback{isolate, cb, std::move(location)} });
		else
			localHandlers.insert({ev, V8::EventCallback{isolate, cb, std::move(location)}});
	}

	void SubscribeRemote(const std::string &ev, v8::Local<v8::Function> cb, V8::SourceLocation &&location)
	{
		if (ev.empty())
			remoteHandlers.insert({ ev, V8::GenericEventCallback{isolate, cb, std::move(location)} });
		else
			remoteHandlers.insert({ ev, V8::EventCallback{isolate, cb, std::move(location)} });
	}

	void UnsubscribeLocal(const std::string &ev, v8::Local<v8::Function> cb)
	{
		if (!ev.empty()) {
			auto range = localHandlers.equal_range(std::string());

			for (auto it = range.first; it != range.second; ++it)
			{
				if (it->second.fn.Get(isolate)->StrictEquals(cb))
					it->second.removed = true;
			}
		}

		auto range = localHandlers.equal_range(ev);

		for (auto it = range.first; it != range.second; ++it)
		{
			if (it->second.fn.Get(isolate)->StrictEquals(cb))
				it->second.removed = true;
		}
	}

	void UnsubscribeRemote(const std::string &ev, v8::Local<v8::Function> cb)
	{
		if (!ev.empty()) {
			auto range = localHandlers.equal_range(std::string());

			for (auto it = range.first; it != range.second; ++it)
			{
				if (it->second.fn.Get(isolate)->StrictEquals(cb))
					it->second.removed = true;
			}
		}

		auto range = remoteHandlers.equal_range(ev);

		for (auto it = range.first; it != range.second; ++it)
		{
			if (it->second.fn.Get(isolate)->StrictEquals(cb))
				it->second.removed = true;
		}
	}

	void DispatchStartEvent(bool error)
	{
		std::vector<v8::Local<v8::Value>> args;
		args.push_back(v8::String::NewFromUtf8(isolate, "resourceStart"));
		args.push_back(v8::Boolean::New(isolate, error));
		InvokeEventHandlers(nullptr, GetLocalHandlers("resourceStart"), args);
	}

	void DispatchStopEvent()
	{
		std::vector<v8::Local<v8::Value>> args;
		args.push_back(v8::String::NewFromUtf8(isolate, "resourceStop"));
		InvokeEventHandlers(nullptr, GetLocalHandlers("resourceStop"), args);
	}

	V8Entity *GetEntity(alt::IBaseObject *handle)
	{
		auto it = entities.find(handle);

		if (it == entities.end())
			return nullptr;

		return it->second;
	}

	V8Entity *CreateEntity(alt::IBaseObject *handle)
	{
		V8Class *_class = V8Entity::GetClass(handle);

		V8Entity *ent = new V8Entity(GetContext(), _class, _class->CreateInstance(GetContext()), handle);
		entities.insert({handle, ent});
		return ent;
	}

	void BindEntity(v8::Local<v8::Object> val, alt::IBaseObject *handle);

	V8Entity *GetOrCreateEntity(alt::IBaseObject *handle, const char *className = "")
	{
		if (!handle)
			Log::Error << __FUNCTION__ << " received invalid handle please contact developers if you see this" << Log::Endl;

		V8Entity *ent = GetEntity(handle);

		if (!ent)
			ent = CreateEntity(handle);

		return ent;
	}

	v8::Local<v8::Value> GetBaseObjectOrNull(alt::IBaseObject *handle);

	template <class T>
	v8::Local<v8::Value> GetBaseObjectOrNull(const alt::Ref<T> &handle)
	{
		return GetBaseObjectOrNull(handle.Get());
	}

	v8::Local<v8::Value> CreateVector3(alt::Vector3f vec);
	v8::Local<v8::Value> CreateRGBA(alt::RGBA rgba);

	bool IsVector3(v8::Local<v8::Value> val);
	bool IsRGBA(v8::Local<v8::Value> val);
	bool IsBaseObject(v8::Local<v8::Value> val);

	void OnCreateBaseObject(alt::Ref<alt::IBaseObject> handle) override;
	void OnRemoveBaseObject(alt::Ref<alt::IBaseObject> handle) override;

	alt::MValue GetFunction(v8::Local<v8::Value> val)
	{
		FunctionImpl *impl = new FunctionImpl{this, val.As<v8::Function>()};
		return alt::ICore::Instance().CreateMValueFunction(impl);
	}

	uint32_t CreateTimer(v8::Local<v8::Context> context, v8::Local<v8::Function> callback, uint32_t interval, bool once, V8::SourceLocation &&location)
	{
		uint32_t id = nextTimerId++;
		//Log::Debug << "Create timer " << id << Log::Endl;
		timers[id] = new V8Timer{isolate, context, GetTime(), callback, interval, once, std::move(location)};

		return id;
	}

	void RemoveTimer(uint32_t id)
	{
		oldTimers.push_back(id);
	}

	uint32_t GetTimersCount()
	{
		return timers.size();
	}

	void NotifyPoolUpdate(alt::IBaseObject *ent);

	v8::Local<v8::Array> GetAllPlayers();
	v8::Local<v8::Array> GetAllVehicles();

	std::vector<V8::EventCallback *> GetLocalHandlers(const std::string &name);
	std::vector<V8::EventCallback *> GetRemoteHandlers(const std::string &name);

	static V8ResourceImpl *Get(v8::Local<v8::Context> ctx)
	{
		alt::IResource *resource = GetResource(ctx);
		return resource
				   ? static_cast<V8ResourceImpl *>(resource->GetImpl())
				   : nullptr;
	}

	static alt::IResource *GetResource(v8::Local<v8::Context> ctx)
	{
		return static_cast<alt::IResource *>(ctx->GetAlignedPointerFromEmbedderData(1));
	}

protected:
	v8::Isolate *isolate;
	alt::IResource *resource;

	v8::Persistent<v8::Context> context;

	std::unordered_map<alt::IBaseObject *, V8Entity *> entities;
	std::unordered_map<uint32_t, V8Timer *> timers;

	std::unordered_multimap<std::string, V8::EventCallback> localHandlers;
	std::unordered_multimap<std::string, V8::EventCallback> remoteHandlers;

	uint32_t nextTimerId = 0;
	std::vector<uint32_t> oldTimers;

	bool playerPoolDirty = true;
	v8::UniquePersistent<v8::Array> players;

	bool vehiclePoolDirty = true;
	v8::UniquePersistent<v8::Array> vehicles;

	V8::PromiseRejections promiseRejections;

	v8::Persistent<v8::Function> vector3Class;
	v8::Persistent<v8::Function> rgbaClass;
	v8::Persistent<v8::Function> baseObjectClass;

	// TEMP
	static int64_t GetTime()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::steady_clock::now().time_since_epoch())
			.count();
	}

	void InvokeEventHandlers(const alt::CEvent *ev, const std::vector<V8::EventCallback *> &handlers, std::vector<v8::Local<v8::Value>> &args);
};
