/*

Copyright (c) Microsoft Corporation
All rights reserved.

Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.

*/

#pragma once

// cpp headers
#include <stdexcept>
#include <exception>
#include <utility>
#include <vector>
#include <memory>
// os headers
// ReSharper disable once CppUnusedIncludeDirective
#include <winsock2.h>
// ReSharper disable once CppUnusedIncludeDirective
#include <ws2ipdef.h>
#include <Iphlpapi.h>
// ctl headers
#include "ctException.hpp"
#include "ctSockaddr.hpp"

namespace ctl
{
	class ctNetAdapterAddresses
	{
	public:
		class iterator
		{
		public:
			////////////////////////////////////////////////////////////////////////////////
			///
			/// c'tor
			/// - NULL ptr is an 'end' iterator
			///
			/// - default d'tor, copy c'tor, and copy assignment
			///
			////////////////////////////////////////////////////////////////////////////////
			iterator() = default;

			explicit iterator(std::shared_ptr<std::vector<BYTE>> _ipAdapter) noexcept
			: buffer(std::move(_ipAdapter))
			{
				if (buffer && !buffer->empty()) {
					current = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(&(this->buffer->at(0)));
				}
			}

			////////////////////////////////////////////////////////////////////////////////
			///
			/// member swap method
			///
			////////////////////////////////////////////////////////////////////////////////
			void swap(_Inout_ iterator& _in) noexcept
			{
				using std::swap;
				swap(this->buffer, _in.buffer);
				swap(this->current, _in.current);
			}

			////////////////////////////////////////////////////////////////////////////////
			///
			/// accessors:
			/// - dereference operators to access the internal row
			///
			////////////////////////////////////////////////////////////////////////////////
			IP_ADAPTER_ADDRESSES& operator*() const
			{
				if (!this->current) {
					throw std::out_of_range("out_of_range: ctNetAdapterAddresses::iterator::operator*");
				}
				return *(this->current);
			}

			IP_ADAPTER_ADDRESSES* operator->() const
			{
				if (!this->current) {
					throw std::out_of_range("out_of_range: ctNetAdapterAddresses::iterator::operator->");
				}
				return this->current;
			}

			////////////////////////////////////////////////////////////////////////////////
			///
			/// comparison and arithmatic operators
			/// 
			/// comparison operators are no-throw/no-fail
			/// arithmatic operators can fail 
			///
			////////////////////////////////////////////////////////////////////////////////
			bool operator==(const iterator& _iter) const noexcept
			{
				// for comparison of 'end' iterators, just look at current
				if (!this->current) {
					return (this->current == _iter.current);
				}

				return ((this->buffer == _iter.buffer) &&
					    (this->current == _iter.current));
			}

			bool operator!=(const iterator& _iter) const noexcept
			{
				return !(*this == _iter);
			}

			// preincrement
			iterator& operator++()
			{
				if (!this->current) {
					throw std::out_of_range("out_of_range: ctNetAdapterAddresses::iterator::operator++");
				}
				// increment
				current = current->Next;
				return *this;
			}

			// postincrement
			iterator operator++(int)
			{
				// ReSharper disable once CppUseAuto
				iterator temp(*this);
				++(*this);
				return temp;
			}

			// increment by integer
			iterator& operator+=(DWORD _inc)
			{
				for (unsigned loop = 0; (loop < _inc) && (this->current != nullptr); ++loop) {
					current = current->Next;
				}
				if (!this->current) {
					throw std::out_of_range("out_of_range: ctNetAdapterAddresses::iterator::operator+=");
				}
				return *this;
			}

			////////////////////////////////////////////////////////////////////////////////
			///
			/// iterator_traits
			/// - allows <algorithm> functions to be used
			///
			////////////////////////////////////////////////////////////////////////////////
			typedef std::forward_iterator_tag   iterator_category;
			typedef IP_ADAPTER_ADDRESSES        value_type;
			typedef int                         difference_type;
			typedef IP_ADAPTER_ADDRESSES*       pointer;
			typedef IP_ADAPTER_ADDRESSES&       reference;

		private:
			std::shared_ptr<std::vector<BYTE>> buffer;
			PIP_ADAPTER_ADDRESSES current = nullptr;
		};

	public:

		////////////////////////////////////////////////////////////////////////////////
		///
		/// c'tor
		///
		/// - default d'tor, copy c'tor, and copy assignment
		/// - Takes an optional _gaaFlags argument which is passed through directly to
		///   GetAdapterAddresses internally - use standard GAA_FLAG_* constants
		///
		////////////////////////////////////////////////////////////////////////////////
		explicit ctNetAdapterAddresses(unsigned _family = AF_UNSPEC, DWORD _gaaFlags = 0) :
			buffer(std::make_shared<std::vector<BYTE>>(16384))
		{
			this->refresh(_family, _gaaFlags);
		}

		////////////////////////////////////////////////////////////////////////////////
		///
		/// refresh
		///
		/// - retrieves the current set of adapter address information
		/// - Takes an optional _gaaFlags argument which is passed through directly to
		///   GetAdapterAddresses internally - use standard GAA_FLAG_* constants
		///
		/// NOTE: this will invalidate any iterators from this instance
		/// NOTE: this only implements the Basic exception guarantee
		///       if this fails, an exception is thrown, and any prior
		///       information is lost. This is still safe to call after errors.
		///
		////////////////////////////////////////////////////////////////////////////////
		void refresh(unsigned _family = AF_UNSPEC, DWORD _gaaFlags = 0) const
		{
			// get both v4 and v6 adapter info
			auto byteSize = static_cast<ULONG>(this->buffer->size());
			auto err = ::GetAdaptersAddresses(
				_family,   // Family
				_gaaFlags, // Flags
				nullptr,   // Reserved
				reinterpret_cast<PIP_ADAPTER_ADDRESSES>(&(this->buffer->at(0))),
				&byteSize
			);
			if (err == ERROR_BUFFER_OVERFLOW) {
				this->buffer->resize(byteSize);
				err = ::GetAdaptersAddresses(
					_family,   // Family
					_gaaFlags, // Flags
					nullptr,   // Reserved
					reinterpret_cast<PIP_ADAPTER_ADDRESSES>(&(this->buffer->at(0))),
					&byteSize
				);
			}
			if (err != NO_ERROR) {
				throw ctException(err, L"GetAdaptersAddresses", L"ctNetAdapterAddresses::ctNetAdapterAddresses", false);
			}
		}

		////////////////////////////////////////////////////////////////////////////////
		///
		/// begin/end
		///
		/// - constructs ctNetAdapterAddresses::iterators
		///
		////////////////////////////////////////////////////////////////////////////////
		iterator begin() const noexcept
		{
			return iterator(this->buffer);
		}

		iterator end() const noexcept
		{
			return iterator();
		}

	private:
		///
		/// private members
		///
		std::shared_ptr<std::vector<BYTE>> buffer;
	};

	///
	/// functor ctNetAdapterMatchingAddrPredicate
	///
	/// Created to leverage STL algorigthms to parse a ctNetAdapterAddresses set of iterators
	/// - to find the first interface that has the specified address assigned
	///
	struct ctNetAdapterMatchingAddrPredicate
	{
		explicit ctNetAdapterMatchingAddrPredicate(ctSockaddr _addr) noexcept :
			targetAddr(std::move(_addr))
		{
		}

		bool operator ()(const IP_ADAPTER_ADDRESSES& _ipAddress) const noexcept
		{
			for (auto unicastAddress = _ipAddress.FirstUnicastAddress;
			     unicastAddress != nullptr;
			     unicastAddress = unicastAddress->Next)
			{
				const ctSockaddr unicastSockaddr(&unicastAddress->Address);
				if (unicastSockaddr == targetAddr) {
					return true;
				}
			}
			return false;
		}

	private:
		ctSockaddr targetAddr;
	};
} // namespace ctl
