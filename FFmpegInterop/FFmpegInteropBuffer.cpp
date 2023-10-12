//*****************************************************************************
//
//	Copyright 2019 Microsoft Corporation
//
//	Licensed under the Apache License, Version 2.0 (the "License");
//	you may not use this file except in compliance with the License.
//	You may obtain a copy of the License at
//
//	http ://www.apache.org/licenses/LICENSE-2.0
//
//	Unless required by applicable law or agreed to in writing, software
//	distributed under the License is distributed on an "AS IS" BASIS,
//	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//	See the License for the specific language governing permissions and
//	limitations under the License.
//
//*****************************************************************************

#include "pch.h"
#include "FFmpegInteropBuffer.h"

using namespace std;

namespace winrt::FFmpegInterop::implementation
{
	FFmpegInteropBuffer::FFmpegInteropBuffer(_In_ AVBufferRef* bufRef) :
		m_length(static_cast<uint32_t>(bufRef->size)),
		m_buf(reinterpret_cast<uint8_t*>(bufRef->data))
	{
		THROW_HR_IF(E_INVALIDARG, bufRef->size > numeric_limits<uint32_t>::max());

		AVBufferRef* bufRefCopy{ av_buffer_ref(bufRef) };
		THROW_IF_NULL_ALLOC(bufRefCopy);
		m_buf.get_deleter() = [AVBufferRef_ptr{ bufRefCopy }](uint8_t*) noexcept { };
	}

	FFmpegInteropBuffer::FFmpegInteropBuffer(_In_ AVBufferRef_ptr bufRef) :
		m_length(static_cast<uint32_t>(bufRef->size)),
		m_buf(reinterpret_cast<uint8_t*>(bufRef->data), [AVBufferRef_ptr{ bufRef.get() }](uint8_t*) noexcept { })
	{
		size_t bufRefSize{ bufRef->size };
		bufRef.release(); // The lambda has taken ownership
		THROW_HR_IF(E_INVALIDARG, bufRefSize > numeric_limits<uint32_t>::max());
	}

	FFmpegInteropBuffer::FFmpegInteropBuffer(_In_ AVPacket_ptr packet) :
		m_length(packet->size),
		m_buf(packet->data)
	{
		if (packet->buf != nullptr)
		{
			// Packet is ref counted. Take ownership of its buffer.
			m_buf.get_deleter() = [AVBufferRef_ptr{ exchange(packet->buf, nullptr) }](uint8_t*) noexcept { };
		}
		else
		{
			// Packet is not ref counted. We need to keep it alive.
			m_buf.get_deleter() = [AVPacket_ptr{ packet.release() }](uint8_t*) noexcept { };
		}
	}

	FFmpegInteropBuffer::FFmpegInteropBuffer(_In_ AVBlob_ptr buf, _In_ uint32_t bufSize) :
		m_length(bufSize),
		m_buf(static_cast<uint8_t*>(buf.get()), [AVBlob_ptr{ buf.get() }](uint8_t*) noexcept { })
	{
		buf.release(); // The lambda has taken ownership
	}

	FFmpegInteropBuffer::FFmpegInteropBuffer(_In_ vector<uint8_t>&& buf) noexcept :
		m_length(static_cast<uint32_t>(buf.size()))
	{
		// We need to move capture buf in the deleter lambda before setting m_buf, 
		// since moving buf invalidates the pointer returned by buf.data().
		uint8_t* p{ nullptr };
		function<void(uint8_t*)> deleter = [pp = &p, buf = move(buf)](uint8_t*) mutable noexcept
		{
			if (pp != nullptr)
			{
				*pp = const_cast<uint8_t*>(buf.data());
				pp = nullptr;
			}
		};

		// Call lambda to set p
		deleter(nullptr);

		m_buf.reset(p);
		m_buf.get_deleter() = move(deleter);
	}

	STDMETHODIMP_(uint32_t) FFmpegInteropBuffer::Capacity() const noexcept
	{
		return m_length;
	}

	STDMETHODIMP_(uint32_t) FFmpegInteropBuffer::Length() const noexcept
	{
		return m_length;
	}

	void FFmpegInteropBuffer::Length(_In_ uint32_t length)
	{
		// We have a fixed length
		THROW_HR(CO_E_NOT_SUPPORTED);
	}

	STDMETHODIMP FFmpegInteropBuffer::Buffer(_Outptr_ uint8_t** buf) noexcept
	{
		RETURN_HR_IF_NULL(E_POINTER, buf);
		*buf = m_buf.get();
		return S_OK;
	}

	STDMETHODIMP FFmpegInteropBuffer::GetUnmarshalClass(
		_In_ REFIID riid,
		_In_opt_ void* pv,
		_In_ DWORD dwDestContext,
		_Reserved_ void* pvDestContext,
		_In_ DWORD mshlflags,
		_Out_ CLSID* pclsid) noexcept
	try
	{
		call_once(m_marshalerInitFlag, &FFmpegInteropBuffer::InitMarshaler, this);
		RETURN_IF_FAILED(m_marshaler->GetUnmarshalClass(riid, pv, dwDestContext, pvDestContext, mshlflags, pclsid));
		return S_OK;
	}
	CATCH_RETURN();

	STDMETHODIMP FFmpegInteropBuffer::GetMarshalSizeMax(
		_In_ REFIID riid,
		_In_opt_ void* pv,
		_In_ DWORD dwDestContext,
		_Reserved_ void* pvDestContext,
		_In_ DWORD mshlflags,
		_Out_ DWORD* pcbSize) noexcept
	try
	{
		call_once(m_marshalerInitFlag, &FFmpegInteropBuffer::InitMarshaler, this);
		RETURN_IF_FAILED(m_marshaler->GetMarshalSizeMax(riid, pv, dwDestContext, pvDestContext, mshlflags, pcbSize));
		return S_OK;
	}
	CATCH_RETURN();

	STDMETHODIMP FFmpegInteropBuffer::MarshalInterface(
		_In_ IStream* pStm,
		_In_ REFIID riid,
		_In_opt_ void* pv,
		_In_ DWORD dwDestContext,
		_Reserved_ void* pvDestContext,
		_In_ DWORD mshlflags) noexcept
	try
	{
		call_once(m_marshalerInitFlag, &FFmpegInteropBuffer::InitMarshaler, this);
		RETURN_IF_FAILED(m_marshaler->MarshalInterface(pStm, riid, pv, dwDestContext, pvDestContext, mshlflags));
		return S_OK;
	}
	CATCH_RETURN();

	STDMETHODIMP FFmpegInteropBuffer::UnmarshalInterface(_In_ IStream* pStm, _In_ REFIID riid, _Outptr_ void** ppv) noexcept
	try
	{
		call_once(m_marshalerInitFlag, &FFmpegInteropBuffer::InitMarshaler, this);
		RETURN_IF_FAILED(m_marshaler->UnmarshalInterface(pStm, riid, ppv));
		return S_OK;
	}
	CATCH_RETURN();

	STDMETHODIMP FFmpegInteropBuffer::ReleaseMarshalData(_In_ IStream* pStm) noexcept
	try
	{
		call_once(m_marshalerInitFlag, &FFmpegInteropBuffer::InitMarshaler, this);
		RETURN_IF_FAILED(m_marshaler->ReleaseMarshalData(pStm));
		return S_OK;
	}
	CATCH_RETURN();

	STDMETHODIMP FFmpegInteropBuffer::DisconnectObject(_Reserved_ DWORD dwReserved) noexcept
	try
	{
		call_once(m_marshalerInitFlag, &FFmpegInteropBuffer::InitMarshaler, this);
		RETURN_IF_FAILED(m_marshaler->DisconnectObject(dwReserved));
		return S_OK;
	}
	CATCH_RETURN();

	void FFmpegInteropBuffer::InitMarshaler()
	{
		THROW_IF_FAILED(RoGetBufferMarshaler(m_marshaler.put()));
	}
}
