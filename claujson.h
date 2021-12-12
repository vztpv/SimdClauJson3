#pragma once


#include <iostream>
#include "simdjson.h" // modified simdjson 0.9.7

#include <map>
#include <vector>
#include <string>
#include <set>
#include <fstream>
#include <iomanip>

namespace claujson {
	using STRING = std::string;
	
	class UserType;

	class Block { // Memory? Block
	public:
		int64_t start = 0;
		int64_t size = 0;
	};

	class PoolManager {
	private:
		UserType* pool = nullptr;
		std::vector<Block> blocks;
		UserType* dead_list_start = nullptr;
		std::vector<UserType*> outOfPool; // pool 밖..
	public:
		enum class Type {
			FROM_STATIC = 0, // no dynamic allocation.
			FROM_POOL, // calloc + free
			FROM_NEW   // new + delete.
		};

		explicit PoolManager() { }

		explicit PoolManager(UserType* pool, std::vector<Block>&& blocks) {
			this->pool = pool;
			this->blocks = std::move(blocks);
		}

		inline void Clear();

		// init - first time only Blocks... -> no Blocks... ?
		void AddBlock(uint64_t start, uint64_t size) {
			Block block{ start, size };
			blocks.push_back(block);
		}

		inline UserType* Alloc();
		inline void DeAlloc(UserType* ut);
	};


	class Data {
	public:
		simdjson::internal::tape_type type;

		bool is_key = false;

		long long int_val;
		unsigned long long uint_val;
		double float_val;
		
		std::string str_val; // const
		


		Data(const Data& other)
			: type(other.type), int_val(other.int_val), uint_val(other.uint_val), float_val(other.float_val), str_val(other.str_val), is_key(other.is_key) {

		}

		Data(Data&& other)
			: type(other.type), int_val(other.int_val), uint_val(other.uint_val), float_val(other.float_val), str_val(std::move(other.str_val)), is_key(other.is_key) {

		}

		Data() : int_val(0), type(simdjson::internal::tape_type::ROOT) { } // here used as ERROR - ROOT

		bool operator==(const Data& other) const {
			if (this->type == other.type) {
				switch (this->type) {
				case simdjson::internal::tape_type::STRING:
					return this->str_val == other.str_val;
					break;
				}
				return true;
			}
			return false;
		}

		bool operator<(const Data& other) const {
			if (this->type == other.type) {
				switch (this->type) {
				case simdjson::internal::tape_type::STRING:
					return this->str_val < other.str_val;
					break;
				}
			}
			return false;
		}

		Data& operator=(const Data& other) {
			if (this == &other) {
				return *this;
			}

			this->type = other.type;
			this->int_val = other.int_val;
			this->uint_val = other.uint_val;
			this->float_val = other.float_val;
			this->str_val = other.str_val;
			this->is_key = other.is_key;

			return *this;
		}


		Data& operator=(Data&& other) {
			if (this == &other) {
				return *this;
			}

			this->type = other.type;
			this->int_val = other.int_val; 
			this->uint_val = other.uint_val;
			this->float_val = other.float_val;
			this->str_val = std::move(other.str_val);
			std::swap(this->is_key, other.is_key);

			return *this;
		}

		friend std::ostream& operator<<(std::ostream& stream, const Data& data) {

			switch (data.type) {
			case simdjson::internal::tape_type::INT64:
				stream << data.int_val;
				break;
			case simdjson::internal::tape_type::UINT64:
				stream << data.uint_val;
				break;
			case simdjson::internal::tape_type::DOUBLE:
				stream << data.float_val;
				break;
			case simdjson::internal::tape_type::STRING:
				stream << data.str_val;
				break;
			case simdjson::internal::tape_type::TRUE_VALUE:
				stream << "true";
				break;
			case simdjson::internal::tape_type::FALSE_VALUE:
				stream << "false";
				break;
			case simdjson::internal::tape_type::NULL_VALUE:
				stream << "null";
				break;
			case simdjson::internal::tape_type::START_ARRAY:
				stream << "[";
				break;
			case simdjson::internal::tape_type::START_OBJECT:
				stream << "{";
				break;
			case simdjson::internal::tape_type::END_ARRAY:
				stream << "]";
				break;
			case simdjson::internal::tape_type::END_OBJECT:
				stream << "}";
				break;
			}

			return stream;
		}
	};

	inline Data Convert(uint64_t* token, const std::unique_ptr<uint8_t[]>& string_buf) {
		uint8_t type = uint8_t((*token) >> 56);
		uint64_t payload = (*token) & simdjson::internal::JSON_VALUE_MASK;
		
		Data data;
		data.is_key = type == (uint8_t)simdjson::internal::tape_type::KEY; // is_key;
		
		if (data.is_key) {
			type = '"';
		}

		data.type = static_cast<simdjson::internal::tape_type>(type);
		
		uint32_t string_length;
		
		switch (type) {
		case '"': // we have a string
			std::memcpy(&string_length, string_buf.get() + payload, sizeof(uint32_t));
			data.str_val = std::string(
				reinterpret_cast<const char*>(string_buf.get() + payload + sizeof(uint32_t)),
				string_length
			);

			break;
		case 'l': // we have a long int
			data.int_val = *(token + 1);
			break;
		case 'u': // we have a long uint
			data.uint_val = *(token + 1);
			break;
		case 'd': // we have a double
			double answer;
			std::memcpy(&answer, token + 1, sizeof(answer));
			data.float_val = answer;
			break;
		case 'n': // we have a null
			break;
		case 't': // we have a true
			break;
		case 'f': // we have a false
			break;
		case '{': // we have an object
			break;    
		case '}': // we end an object
			break;
		case '[': // we start an array
			break;
		case ']': // we end an array
			break;
		case 'r':
			break;
		default:
			break;
		}

		return data;
	}

	class UserType {
	private:
		inline UserType* make_user_type(UserType* pool, const claujson::Data& name, int type) const {
			new (pool) UserType(name, type);
			pool->alloc_type = PoolManager::Type::FROM_POOL;
			return pool;
		}

		inline UserType* make_user_type(UserType* pool, int type) const {
			new (pool) UserType(claujson::Data(), type);
			pool->alloc_type = PoolManager::Type::FROM_POOL;
			return pool;
		}

		inline UserType* make_user_type(UserType* pool, claujson::Data&& name, int type) const {
			new (pool) UserType(std::move(name), type);
			pool->alloc_type = PoolManager::Type::FROM_POOL;
			return pool;
		}


		inline UserType* make_item_type(UserType* pool, const claujson::Data& value) const {
			new (pool) UserType(value, 4);
			pool->alloc_type = PoolManager::Type::FROM_POOL;
			return pool;
		}
		
		inline UserType* make_item_type(UserType* pool, claujson::Data&& value) const {
			new (pool) UserType(std::move(value), 4);
			pool->alloc_type = PoolManager::Type::FROM_POOL;
			return pool;
		}
	public:


		inline static UserType* make_object(PoolManager& manager, const Data& x) {
			UserType* temp = manager.Alloc();
			new (temp) UserType(std::move(x), 0);
			return temp;
		}
		
		inline static UserType* make_array(PoolManager& manager, const Data& x) {
			UserType* temp = manager.Alloc();
			new (temp) UserType(std::move(x), 1);
			return temp;
		}

		void set_value(const STRING& value) {
			this->value.str_val = value;
		}
		
		void set_value(const Data& data) {
			this->value = data;
		}

		UserType* clone() const {
			UserType* temp = new UserType(this->value);

			temp->type = this->type;

			temp->parent = nullptr; // chk!

			temp->data.reserve(this->data.size());

			for (auto x : this->data) {
				temp->data.push_back(x->clone());
			}

			return temp;
		}
		
	private:
		UserType* next_dead = nullptr; // for linked list.
		
		friend PoolManager;

		PoolManager::Type alloc_type;
		uint64_t alloc_idx = 0;

		claujson::Data value; // equal to key
		std::vector<UserType*> data; 
		int type = -1; // 0 - object, 1 - array, 2 - virtual object, 3 - virtual array, 4 - item, -1 - root  -2 - only in parse...
		UserType* parent = nullptr;
	public:
		//inline const static size_t npos = -1; // ?
		// chk type?
		bool operator<(const UserType& other) const {
			return value.str_val < other.value.str_val;
		}
		bool operator==(const UserType& other) const {
			return value.str_val == other.value.str_val;
		}

	public:

		inline const std::vector<UserType*>& get_data() const { return data; }
		inline std::vector<UserType*>& get_data() { return data; }

		UserType* find(std::string_view key) {
			for (size_t i = 0; i < data.size(); ++i) {
				if (data[i]->value.is_key && data[i]->value.str_val == key) {
					return data[i];
				}
			}
			return nullptr;
		}

		const UserType* find(std::string_view key) const {
			for (size_t i = 0; i < data.size(); ++i) {
				if (data[i]->value.is_key && data[i]->value.str_val == key) {
					return data[i];
				}
			}
			return nullptr;
		}

	public:
		UserType(const UserType& other)
			: value(other.value),
			type(other.type), parent(other.parent)
		{
			this->data.reserve(other.data.size());
			for (auto& x : other.data) {
				this->data.push_back(x->clone());
			}
		}
		

		UserType(UserType&& other) {
			value = std::move(other.value);
			this->data = std::move(other.data);
			type = std::move(other.type);
			parent = std::move(other.parent);
		}

		UserType& operator=(UserType&& other) noexcept {
			if (this == &other) {
				return *this;
			}

			value = std::move(other.value);
			data = std::move(other.data);
			type = std::move(other.type);
			parent = std::move(other.parent);

			return *this;
		}
		
		const Data& get_value() const { return value; }


	private:
		void LinkUserType(UserType* ut) // friend?
		{
			data.push_back(ut);

			ut->parent = this;
		}
		void LinkItemType(UserType* key, UserType* data) {
			this->data.push_back(key);
			this->data.push_back(data);
		}
		void LinkItemType(UserType* data) {
			this->data.push_back(data);
		}
	private:
		UserType(claujson::Data&& value, int type = -1) : value(std::move(value)), type(type)
		{

		}
		
		UserType(const claujson::Data& value, int type = -1) : value(value), type(type)
		{
			//
		}
	public:
		UserType() : type(-1) {
			//
		}
		virtual ~UserType() {
			value = Data();
		}
	public:

		bool is_object() const {
			return type == 0 || type == 2;
		}

		bool is_array() const {
			return type == 1 || type == 3 || type  == -1 || type == -2;
		}

		bool is_in_root() const {
			return get_parent()->type == -1;
		}

		bool is_item_type() const {
			return type == 4;
		}

		bool is_user_type() const {
			return is_object() || is_array();
		}

		bool is_root() const {
			return type == -1;
		}

		// name key check?
		void add_object_element(PoolManager& manager, const claujson::Data& name, const claujson::Data& data) {
			// todo - chk this->type == 0 (object) but name is empty
			// todo - chk this->type == 1 (array) but name is not empty.

			if (this->type == 1) {
				throw "Error add object element to array in add_object_element ";
			}
			if (this->type == -1 && this->data.size() >= 1) {
				throw "Error not valid json in add_object_element";
			}

			this->data.push_back(make_item_type(manager.Alloc(), name));
			this->data.push_back(make_item_type(manager.Alloc(), data));
		}
		
		void add_array_element(PoolManager& manager, const claujson::Data& data) {
			// todo - chk this->type == 0 (object) but name is empty
			// todo - chk this->type == 1 (array) but name is not empty.

			if (this->type == 0) {
				throw "Error add object element to array in add_array_element ";
			}
			if (this->type == -1 && this->data.size() >= 1) {
				throw "Error not valid json in add_array_element";
			}

			this->data.push_back(make_item_type(manager.Alloc(), data)); // (Type*)make_item_type(std::move(temp), data));
		}

		void remove_all(PoolManager& manager, UserType* ut) {
			for (size_t i = 0; i < ut->data.size(); ++i) {
				if (ut->data[i]) {
					manager.DeAlloc(ut);

					remove_all(manager, ut->data[i]);
					ut->data[i] = nullptr;
				}
			}
			ut->data.clear();
		}
	
		void remove_all(PoolManager& manager) {
			remove_all(manager, this);
		}

	private:

		//todo..
		void remove_all(UserType* ut) {
			for (size_t i = 0; i < ut->data.size(); ++i) {
				if (ut->data[i]) {
					//remove_all(ut->data[i]);
					ut->data[i] = nullptr;
				}
			}
			ut->data.clear();
			ut->value = Data();
		}

		void remove_all() {
			remove_all(this);
		}
	public:

		void add_object_with_key(UserType* object) {
			const auto& name = object->value;

			if (is_array()) {
				throw "Error in add_object_with_key";
			}

			if (this->type == -1 && this->data.size() >= 1) {
				throw "Error not valid json in add_object_with_key";
			}

			this->data.push_back(object);
			((UserType*)this->data.back())->parent = this;
		}

		void add_array_with_key(UserType* _array) {
			const auto& name = _array->value;

			if (is_array()) {
				throw "Error in add_array_with_key";
			}

			if (this->type == -1 && this->data.size() >= 1) {
				throw "Error not valid json in add_array_with_key";
			}

			this->data.push_back(_array);
			((UserType*)this->data.back())->parent = this;
		}

		void add_object_with_no_key(UserType* object) {
			const Data& name = object->value;

			if (is_object()) {
				throw "Error in add_object_with_no_key";
			}

			if (this->type == -1 && this->data.size() >= 1) {
				throw "Error not valid json in add_object_with_no_key";
			}

			this->data.push_back(object);
			((UserType*)this->data.back())->parent = this;
		}

		void add_array_with_no_key(UserType* _array) {
			const Data& name = _array->value;

			if (is_object()) {
				throw "Error in add_array_with_no_key";
			}

			if (this->type == -1 && this->data.size() >= 1) {
				throw "Error not valid json in add_array_with_no_key";
			}

			this->data.push_back(_array);
			((UserType*)this->data.back())->parent = this;
		}

		void reserve_data_list(size_t len) {
			data.reserve(len);
		}

	private:

		__forceinline void add_user_type(UserType* ut) {
			this->data.push_back(ut);
			ut->parent = this;
		}


		__forceinline static UserType make_none() {
			Data temp;
			temp.str_val = "";
			temp.type = simdjson::internal::tape_type::STRING;

			UserType ut(std::move(temp), -2);

			return ut;
		}

		__forceinline bool is_virtual() const {
			return type == 2 || type == 3;
		}

		__forceinline static UserType make_virtual_object() {
			UserType ut;
			ut.type = 2;
			return ut;
		}

		__forceinline static UserType make_virtual_array() {
			UserType ut;
			ut.type = 3;
			return ut;
		}
		
		__forceinline void add_user_type(UserType* pool, const Data& name, int type) {
			// todo - chk this->type == 0 (object) but name is empty
			// todo - chk this->type == 1 (array) but name is not empty.
			// todo - chk this->type == -1 .. one object or one array or data(true or false or null or string or number).


			//if (this->type == -1 && this->data.size() >= 1) {
			//	throw "Error not valid json in add_user_type";
			//}

			this->data.push_back(make_user_type(pool, name, type));

			((UserType*)this->data.back())->parent = this;
		}
		
		__forceinline void add_user_type(UserType* pool, int type) {
			// todo - chk this->type == 0 (object) but name is empty
			// todo - chk this->type == 1 (array) but name is not empty.
			// todo - chk this->type == -1 .. one object or one array or data(true or false or null or string or number).


			//if (this->type == -1 && this->data.size() >= 1) {
			//	throw "Error not valid json in add_user_type";
			//}

			this->data.push_back(make_user_type(pool, type));

			((UserType*)this->data.back())->parent = this;
			
		}

		__forceinline void add_user_type(UserType* pool, Data&& name, int type) {
			// todo - chk this->type == 0 (object) but name is empty
			// todo - chk this->type == 1 (array) but name is not empty.
			// todo - chk this->type == -1 .. one object or one array or data(true or false or null or string or number).

			//if (this->type == -1 && this->data.size() >= 1) {
			//	throw "Error not valid json in add_user_type";
			//}

			this->data.push_back(make_user_type(pool, std::move(name), type));
			((UserType*)this->data.back())->parent = this;
		}

		// add item_type in object? key = value
		__forceinline void add_item_type(UserType* pool, Data&& name, claujson::Data&& data) {
			// todo - chk this->type == 0 (object) but name is empty
			// todo - chk this->type == 1 (array) but name is not empty.

			//if (this->type == -1 && this->data.size() >= 1) {
			//	throw "Error not valid json in add_item_type";
			//}

			{
				this->data.push_back(make_item_type(pool, std::move(name)));
			}

			this->data.push_back(make_item_type(pool + 1, std::move(data)));
		}

		__forceinline void add_item_type(UserType* pool, claujson::Data&& data) {
			// todo - chk this->type == 0 (object) but name is empty
			// todo - chk this->type == 1 (array) but name is not empty.

			//if (this->type == -1 && this->data.size() >= 1) {
			//	throw "Error not valid json in add_item_type";
			//}

			this->data.push_back(make_item_type(pool, std::move(data)));
		}

		__forceinline void add_item_type(UserType* pool, const Data& name, claujson::Data&& data) {
			// todo - chk this->type == 0 (object) but name is empty
			// todo - chk this->type == 1 (array) but name is not empty.

			//if (this->type == -1 && this->data.size() >= 1) {
			//	throw "Error not valid json in add_item_type";
			//}

			this->data.push_back(make_item_type(pool, name));
			this->data.push_back(make_item_type(pool + 1, std::move(data)));
		}

		__forceinline void add_item_type(UserType* pool, const Data& name, const claujson::Data& data) {
			// todo - chk this->type == 0 (object) but name is empty
			// todo - chk this->type == 1 (array) but name is not empty.

		//	if (this->type == -1 && this->data.size() >= 1) {
			//	throw "Error not valid json in add_item_type";
		//	}

			this->data.push_back(make_item_type(pool, name));
			this->data.push_back(make_item_type(pool + 1, data));
		}

		__forceinline void add_item_type(UserType* pool, const claujson::Data& data) {
			// todo - chk this->type == 0 (object) but name is empty
			// todo - chk this->type == 1 (array) but name is not empty.

			//if (this->type == -1 && this->data.size() >= 1) {
			//	throw "Error not valid json in add_item_type";
			//}

			this->data.push_back(make_item_type(pool, data));
		}
		
	public:

		UserType*& get_data_list(size_t idx) {
			return this->data[idx];
		}
		const UserType* const& get_data_list(size_t idx) const {
			return this->data[idx];
		}

		size_t get_data_size() const {
			return this->data.size();
		}

		
		void remove_data_list(PoolManager& manager, size_t idx) {
			manager.DeAlloc(data[idx]);
			data.erase(data.begin() + idx);
		}


		UserType* get_parent() {
			return parent;
		}

		const UserType* get_parent() const {
			return parent;
		}

		friend class LoadData;
	};


	inline void PoolManager::Clear() {
		if (pool) {
			free(pool); //
		}
		pool = nullptr;
		blocks.clear();
		dead_list_start = nullptr;
		for (size_t i = 0; i < outOfPool.size(); ++i) {
			delete outOfPool[i];
		}
		outOfPool.clear();
	}

	inline UserType* PoolManager::Alloc() {
		// 1. find space for dead_list.
		if (dead_list_start) {
			UserType* x = dead_list_start;
			dead_list_start = dead_list_start->next_dead;

			new (x) UserType();
			x->alloc_type = PoolManager::Type::FROM_POOL;
			return x;
		}

		// 2. find space for blocks.
		for (uint64_t i = 0; i < blocks.size(); ++i) {
			if (blocks[i].size > 0) {
				UserType* x = pool + blocks[i].start;

				++blocks[i].start;
				--blocks[i].size;

				new (x) UserType();
				x->alloc_type = PoolManager::Type::FROM_POOL;
				return x;
			}
		}

		// 3. new in out of pool. (new)
		outOfPool.push_back(new UserType());
		outOfPool.back()->alloc_type = PoolManager::Type::FROM_NEW;
		outOfPool.back()->alloc_idx = outOfPool.size() - 1;
		return outOfPool.back();
	}

	inline void PoolManager::DeAlloc(UserType* ut) {
		// 1-1. from pool?
		if (ut->alloc_type == PoolManager::Type::FROM_POOL) {
			// 2. add dead_list..
			ut->next_dead = this->dead_list_start;
			this->dead_list_start = ut->next_dead;
		}
		// 1-2. from_outOfPool?
		else if (ut->alloc_type == PoolManager::Type::FROM_NEW) {
			// swap and pop_back..
			this->outOfPool.back()->alloc_idx = ut->alloc_idx;
			std::swap(this->outOfPool[ut->alloc_idx], this->outOfPool.back());
			this->outOfPool.pop_back();
		}
		else { // STATIC
			// nothing.
		}
	}

	class LoadData
	{
	public:
		static int Merge(class UserType* next, class UserType* ut, class UserType** ut_next)
		{

			// check!!
			while (ut->get_data_size() >= 1
				&& (ut->get_data_list(0)->is_user_type()) && (ut->get_data_list(0))->is_virtual())
			{
				ut = (UserType*)ut->get_data_list(0);
			}

			bool chk_ut_next = false;

			while (true) {

				class UserType* _ut = ut;
				class UserType* _next = next;


				if (ut_next && _ut == *ut_next) {
					*ut_next = _next;
					chk_ut_next = true;
				}

				size_t _size = _ut->get_data_size(); // bug fix.. _next == _ut?
				for (size_t i = 0; i < _size; ++i) {
					if (_ut->get_data_list(i)->is_user_type()) {
						if (((UserType*)_ut->get_data_list(i))->is_virtual()) {
							//_ut->get_user_type_list(i)->used();
						}
						else {
							if (_next->is_array() && _ut->get_data_list(i)->get_value().is_key) {
								std::cout << "chk ";
							}
							_next->LinkUserType(_ut->get_data_list(i));
							_ut->get_data_list(i) = nullptr;
						}
					}
					else { // item type.
						if (_ut->get_data_list(i)->get_value().is_key) { 
							
							_next->LinkItemType(std::move(_ut->get_data_list(i)), std::move(_ut->get_data_list(i + 1)));
							++i;
						}
						else {
							_next->LinkItemType(std::move(_ut->get_data_list(i)));
						}
					}
				}

				_ut->remove_all();

				ut = ut->get_parent();
				next = next->get_parent();


				if (next && ut) {
					//
				}
				else {
					// right_depth > left_depth
					if (!next && ut) {
						return -1;
					}
					else if (next && !ut) {
						return 1;
					}

					return 0;
				}
			}
		}

	private:
		static bool __LoadData(claujson::UserType* _pool, const std::unique_ptr<uint8_t[]>& string_buf, const std::unique_ptr<uint64_t[]>& token_arr,
			int64_t token_arr_start, size_t token_arr_len, class UserType* _global,
			int start_state, int last_state, class UserType** next, int* err, int no, UserType*& after_pool)
		{
			//int a = clock();

			UserType* pool = _pool + token_arr_start;

			std::vector<uint64_t*> Vec;

			if (token_arr_len <= 0) {
				*next = nullptr;
				return false;
			}

			class UserType& global = *_global;

			int state = start_state;
			size_t braceNum = 0;
			std::vector< class UserType* > nestedUT(1);

			nestedUT.reserve(10);
			nestedUT[0] = &global;

			int64_t count = 0;

			uint64_t* key = nullptr;

			for (int64_t i = 0; i < token_arr_len; ++i) {

				const simdjson::internal::tape_type type = 
					static_cast<simdjson::internal::tape_type>((((token_arr)[token_arr_start + i]) >> 56));

				uint64_t payload = (token_arr[token_arr_start + i]) & simdjson::internal::JSON_VALUE_MASK;


				switch (state)
				{
				case 0:
				{
					// Left 1
					if (type == simdjson::internal::tape_type::START_OBJECT ||
						type == simdjson::internal::tape_type::START_ARRAY) { // object start, array start

						if (!Vec.empty()) {

							if (static_cast<simdjson::internal::tape_type>((*Vec[0]) >> 56) == simdjson::internal::tape_type::KEY) {
								for (size_t x = 0; x < Vec.size(); x += 2) {
									nestedUT[braceNum]->add_item_type(pool, Convert(Vec[x], string_buf), Convert(Vec[x + 1], string_buf));
									++pool; ++pool;
								}
							}
							else {
								for (size_t x = 0; x < Vec.size(); x += 1) {
									nestedUT[braceNum]->add_item_type(pool, Convert(Vec[x], string_buf));
									++pool;
								}
							}

							Vec.clear();
						}

						if (key) {
							nestedUT[braceNum]->add_user_type(pool, Convert(key, string_buf), type == simdjson::internal::tape_type::START_OBJECT ? 0 : 1); // object vs array
							key = nullptr; ++pool;
						}
						else {
							nestedUT[braceNum]->add_user_type(pool, type == simdjson::internal::tape_type::START_OBJECT ? 0 : 1);
							++pool;
						}


						class UserType* pTemp = nestedUT[braceNum]->get_data_list(nestedUT[braceNum]->get_data_size() - 1);
						
						if (pTemp->is_array()) {
							pTemp->reserve_data_list(((payload >> 32) & simdjson::internal::JSON_COUNT_MASK));
						}
						else {
							pTemp->reserve_data_list(2 * ((payload >> 32) & simdjson::internal::JSON_COUNT_MASK));
						}

						braceNum++;

						/// new nestedUT
						if (nestedUT.size() == braceNum) {
							nestedUT.push_back(nullptr);
						}

						/// initial new nestedUT.
						nestedUT[braceNum] = pTemp;

						state = 0;

					}
					// Right 2
					else if (type == simdjson::internal::tape_type::END_OBJECT ||
						type == simdjson::internal::tape_type::END_ARRAY) {

						state = 0; 

						if (!Vec.empty()) {
							if (type == simdjson::internal::tape_type::END_OBJECT) {
								for (size_t x = 0; x < Vec.size(); x += 2) {
									nestedUT[braceNum]->add_item_type(pool, Convert(Vec[x], string_buf), Convert(Vec[x + 1], string_buf));
									++pool; ++pool;
									
								}
							}
							else { // END_ARRAY
								for (size_t x = 0; x < Vec.size(); x += 1) {
									nestedUT[braceNum]->add_item_type(pool, Convert(Vec[x], string_buf));
									++pool;
								}
							}

							Vec.clear();
						}


						if (braceNum == 0) {
							class UserType ut; //

							ut.add_user_type(pool, type == simdjson::internal::tape_type::END_OBJECT ? 2 : 3); // json -> "var_name" = val  
							++pool;

							for (size_t i = 0; i < nestedUT[braceNum]->get_data_size(); ++i) {
								ut.get_data_list(0)->add_user_type(nestedUT[braceNum]->get_data_list(i));
								nestedUT[braceNum]->get_data_list(i) = nullptr;
							}

							nestedUT[braceNum]->remove_all();
							nestedUT[braceNum]->add_user_type(ut.get_data_list(0));

							ut.get_data_list(0) = nullptr;

							braceNum++;
							{
								uint64_t now_tape_val = token_arr[token_arr_start + i];
								uint32_t before_idx = uint32_t(now_tape_val & simdjson::internal::JSON_VALUE_MASK);
								uint64_t before_tape_val = token_arr[before_idx];
								uint64_t before_payload = before_tape_val & simdjson::internal::JSON_VALUE_MASK;

								// just child count?
								if (type == simdjson::internal::tape_type::END_OBJECT) {
									nestedUT[0]->reserve_data_list(2 * ((before_payload >> 32)& simdjson::internal::JSON_COUNT_MASK));
								}
								else if (type == simdjson::internal::tape_type::END_ARRAY) {
									nestedUT[0]->reserve_data_list(((before_payload >> 32) & simdjson::internal::JSON_COUNT_MASK));
								}
							}
						}
						
						{						
							if (braceNum < nestedUT.size()) {
								nestedUT[braceNum] = nullptr;
							}

							braceNum--;
						}
					}
					else {
						{

							uint64_t* data = &token_arr[token_arr_start + i]; // Convert(&(token_arr[token_arr_start + i]), string_buf);
							
							if (type == simdjson::internal::tape_type::KEY) {
								const simdjson::internal::tape_type _type =
									static_cast<simdjson::internal::tape_type>((((token_arr)[token_arr_start + i + 1]) >> 56));
								
								
								if (_type == simdjson::internal::tape_type::START_ARRAY || _type == simdjson::internal::tape_type::START_OBJECT) {
									key = data;
								}
								else {
									Vec.push_back(data);
								}
							}
							else {	//std::cout << data.str_val << " \n ";
								Vec.push_back(data);
							}
							
							state = 0;
						}	
					}
				}
				break;
				default:
					// syntax err!!
					*err = -1;
					return false; // throw "syntax error ";
					break;
				}
			
				switch ((int)type) {
				case '"': // we have a string
					//os << "string \"";
				   // std::memcpy(&string_length, string_buf.get() + payload, sizeof(uint32_t));
				   // os << internal::escape_json_string(std::string_view(
				   //     reinterpret_cast<const char*>(string_buf.get() + payload + sizeof(uint32_t)),
				   //     string_length
				   // ));
				   // os << '"';
				  //  os << '\n';
					
					break;
				case 'l': // we have a long int
					++i;

					break;
				case 'u': // we have a long uint
					++i;
					break;
				case 'd': // we have a double
					++i;
					break;
				case 'n': // we have a null
				   // os << "null\n";
					break;
				case 't': // we have a true
				   // os << "true\n";
					break;
				case 'f': // we have a false
				  //  os << "false\n";
					break;
				case '{': // we have an object
				 //   os << "{\t// pointing to next tape location " << uint32_t(payload)
				 //       << " (first node after the scope), "
				  //      << " saturated count "
				   //     << ((payload >> 32) & internal::JSON_COUNT_MASK) << "\n";

					
					break;
				case '}': // we end an object
				  //  os << "}\t// pointing to previous tape location " << uint32_t(payload)
				  //      << " (start of the scope)\n";
					
					break;
				case '[': // we start an array
				  //  os << "[\t// pointing to next tape location " << uint32_t(payload)
				  //      << " (first node after the scope), "
				  //      << " saturated count "
				   //     << ((payload >> 32) & internal::JSON_COUNT_MASK) << "\n";
					
					break;
				case ']': // we end an array
				 //   os << "]\t// pointing to previous tape location " << uint32_t(payload)
				  //      << " (start of the scope)\n";
				
					break;
				case 'r': // we start and end with the root node
				  // should we be hitting the root node?
					break;
				default:

					break;
				}
			}

			if (next) {
				*next = nestedUT[braceNum];
			}

			if (Vec.empty() == false) {
				if (static_cast<simdjson::internal::tape_type>((*Vec[0]) >> 56) == simdjson::internal::tape_type::KEY) {
					for (size_t x = 0; x < Vec.size(); x += 2) {
						nestedUT[braceNum]->add_item_type(pool, Convert(Vec[x], string_buf), Convert(Vec[x + 1], string_buf));
						++pool; ++pool;
					}
				}
				else {
					for (size_t x = 0; x < Vec.size(); x += 1) {
						nestedUT[braceNum]->add_item_type(pool, Convert(Vec[x], string_buf));
						++pool;
					}
				}

				Vec.clear();
			}

			if (state != last_state) {
				*err = -2;
				return false;
				// throw STRING("error final state is not last_state!  : ") + toStr(state);
			}

			after_pool = pool;
			//int b = clock();
			//std::cout << "parse thread " << b - a << "ms\n";
			return true;
		}

		static int64_t FindDivisionPlace(const std::unique_ptr<uint8_t[]>& string_buf, const std::unique_ptr<uint64_t[]>& token_arr, int64_t start, int64_t last)
		{
			for (int64_t a = start; a <= last; ++a) {
				auto& x = token_arr[a];
				const simdjson::internal::tape_type type = static_cast<simdjson::internal::tape_type>(x >> 56);
				bool key = false;
				bool next_is_valid = false;

				switch ((int)type) {
				case 'l': // we have a long int
				case 'u': // we have a long uint
				case 'd': // we have a double
					break;
				case 'k': // key
					key = true;
				default:
					// error?
					next_is_valid = true;
					break;
				}

				if (next_is_valid && a + 1 <= last) {
					auto& x = token_arr[a + 1];
					const simdjson::internal::tape_type next_type = static_cast<simdjson::internal::tape_type>(x >> 56);

					if (next_type == simdjson::internal::tape_type('}')
						|| next_type == simdjson::internal::tape_type(']')) {
						return a + 1;
					}
					if (next_type == simdjson::internal::tape_type::KEY) {
						return a + 1;
					}
				}
			}
			return -1;
		}
	public:

		static bool _LoadData(claujson::UserType* pool, class UserType& global, const std::unique_ptr<uint8_t[]>& string_buf, const std::unique_ptr<uint64_t[]>& token_arr, int64_t& length,
			std::vector<int64_t>& start, const int parse_num, std::vector<Block>& blocks) // first, strVec.empty() must be true!!
		{
			const int pivot_num = parse_num - 1;
			//size_t token_arr_len = length; // size?

			class UserType* before_next = nullptr;
			class UserType _global;

			bool first = true;
			int64_t sum = 0;

			{
				std::set<int64_t> _pivots;
				std::vector<int64_t> pivots;
				//const int64_t num = token_arr_len; //

				if (pivot_num > 0) {
					std::vector<int64_t> pivot;
					pivots.reserve(pivot_num);
					pivot.reserve(pivot_num);

					pivot.push_back(start[0]);

					for (int i = 1; i < parse_num; ++i) {
						pivot.push_back(FindDivisionPlace(string_buf, token_arr, start[i], start[i + 1] - 1));
					}

					for (size_t i = 0; i < pivot.size(); ++i) {
						if (pivot[i] != -1) {
							_pivots.insert(pivot[i]);
						}
					}

					for (auto& x : _pivots) {
						pivots.push_back(x);
					}

					pivots.push_back(length - 1);
				}
				else {
					pivots.push_back(start[0]);
					pivots.push_back(length - 1);
				}

				std::vector<class UserType*> next(pivots.size() - 1, nullptr);
				{

					std::vector<class UserType> __global(pivots.size() - 1);
					for (int i = 0; i < __global.size(); ++i) {
						__global[i].type = -2;
					}
					
					std::vector<std::thread> thr(pivots.size() - 1);

					std::vector<class UserType*> after_pool(pivots.size() - 1, nullptr);

					std::vector<int> err(pivots.size() - 1, 0);
					{
						int64_t idx = pivots.size() < 2 ? length - 1 : pivots[1] - pivots[0];
						int64_t _token_arr_len = idx;

						thr[0] = std::thread(__LoadData, pool, std::ref(string_buf), std::ref(token_arr), start[0], _token_arr_len, &__global[0], 0, 0,
							&next[0], &err[0], 0, std::ref(after_pool[0]));
					}

					for (size_t i = 1; i < pivots.size() - 1; ++i) {
						int64_t _token_arr_len = pivots[i + 1] - pivots[i];

						thr[i] = std::thread(__LoadData, pool, std::ref(string_buf), std::ref(token_arr), pivots[i], _token_arr_len, &__global[i], 0, 0,
							&next[i], &err[i], i, std::ref(after_pool[i]));
					}


					auto a = std::chrono::steady_clock::now();

					// wait
					for (size_t i = 0; i < thr.size(); ++i) {
						thr[i].join();
					}

					for (int i = 0; i < start.size() - 1; ++i) {
						blocks.push_back(Block{ after_pool[i] - pool, start[i + 1] - (after_pool[i] - pool) });
					}

					auto b = std::chrono::steady_clock::now();
					auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(b - a);
					std::cout << "parse1 " << dur.count() << "ms\n";

					for (size_t i = 0; i < err.size(); ++i) {
						switch (err[i]) {
						case 0:
							break;
						case -1:
						case -4:
							std::cout << "Syntax Error\n"; return false;
							break;
						case -2:
							std::cout << "error final state is not last_state!\n"; return false;
							break;
						case -3:
							std::cout << "error x > buffer + buffer_len:\n"; return false;
							break;
						default:
							std::cout << "unknown parser error\n"; return false;
							break;
						}
					}

					// Merge
					//try
					{
						int i = 0;
						std::vector<int> chk(parse_num, 0);
						auto x = next.begin();
						auto y = __global.begin();
						while (true) {
							if (y->get_data_size() + y->get_data_size() == 0) {
								chk[i] = 1;
							}

							++x;
							++y;
							++i;

							if (x == next.end()) {
								break;
							}
						}

						int start = 0; 
						int last = pivots.size() - 1 - 1;

						for (int i = 0; i < pivots.size() - 1; ++i) {
							if (chk[i] == 0) {
								start = i;
								break;
							}
						}

						for (int i = pivots.size() - 1 - 1; i >= 0; --i) {
							if (chk[i] == 0) {
								last = i;
								break;
							}
						}

						if (__global[start].get_data_size() > 0 && __global[start].get_data_list(0)->is_user_type()
							&& ((UserType*)__global[start].get_data_list(0))->is_virtual()) {
							std::cout << "not valid file1\n";
							throw 1;
						}
						if (next[last] && next[last]->get_parent() != nullptr) {
							std::cout << "not valid file2\n";
							throw 2;
						}



						int err = Merge(&_global, &__global[start], &next[start]);
						if (-1 == err || (pivots.size() == 0 && 1 == err)) {
							std::cout << "not valid file3\n";
							throw 3;
						}

						for (int i = start + 1; i <= last; ++i) {

							if (chk[i]) {
								continue;
							}

							// linearly merge and error check...
							int before = i - 1;
							for (int k = i - 1; k >= 0; --k) {
								if (chk[k] == 0) {
									before = k;
									break;
								}
							}
							
							int err = Merge(next[before], &__global[i], &next[i]);

							if (-1 == err) {
								std::cout << "chk " << i << " " << __global.size() << "\n";
								std::cout << "not valid file4\n";
								throw 4;
							}
							else if (i == pivots.size() && 1 == err) {					
								std::cout << "not valid file5\n";
								throw 5;
							}
						}
					}
					//catch (...) {
						//throw "in Merge, error";
					//	return false;
					//}
					//
					before_next = next.back();

					auto c = std::chrono::steady_clock::now();
					auto dur2 = std::chrono::duration_cast<std::chrono::nanoseconds>(c - b);
					std::cout << "parse2 " << dur2.count() << "ns\n";
				}
			}
			//int a = clock();

			Merge(&global, &_global, nullptr);

			/// global = std::move(_global);
			//int b = clock();
			//std::cout << "chk " << b - a << "ms\n";
			return true;
		}
		static bool parse(claujson::UserType* pool, class UserType& global, const std::unique_ptr<uint8_t[]>& string_buf, const std::unique_ptr<uint64_t[]>& tokens, 
			int64_t length, std::vector<int64_t>& start, int thr_num, std::vector<Block>& blocks) {
			return LoadData::_LoadData(pool, global, string_buf, tokens, length, start, thr_num, blocks);
		}

		//
		static void _save(std::ostream& stream, UserType* ut, const int depth = 0) {
			if (!ut) { return; }

			if (ut->is_object()) {
				for (size_t i = 0; i < ut->get_data_size(); ++i) {
					if (ut->get_data_list(i)->is_user_type()) {
						auto& x = ut->get_data_list(i)->value;

						if (
							x.type == simdjson::internal::tape_type::STRING) {
							stream << "\"";
							for (long long j = 0; j < x.str_val.size(); ++j) {
								switch (x.str_val[j]) {
								case '\\':
									stream << "\\\\";
									break;
								case '\"':
									stream << "\\\"";
									break;
								case '\n':
									stream << "\\n";
									break;

								default:
									if (isprint(x.str_val[j]))
									{
										stream << x.str_val[j];
									}
									else
									{
										int code = x.str_val[j];
										if (code > 0 && (code < 0x20 || code == 0x7F))
										{
											char buf[] = "\\uDDDD";
											sprintf(buf + 2, "%04X", code);
											stream << buf;
										}
										else {
											stream << x.str_val[j];
										}
									}
								}
							}

							stream << "\"";

							if (x.is_key) {
								stream << " : ";
							}
						}
						else {
							std::cout << "Error : no key\n";
						}
						stream << " ";

						if (((UserType*)ut->get_data_list(i))->is_object()) {
							stream << " { \n";
						}
						else {
							stream << " [ \n";
						}

						_save(stream, (UserType*)ut->get_data_list(i), depth + 1);

						if (((UserType*)ut->get_data_list(i))->is_object()) {
							stream << " } \n";
						}
						else {
							stream << " ] \n";
						}
					}
					else {
						auto& x = ut->get_data_list(i)->value;

						if (
							x.type == simdjson::internal::tape_type::STRING) {
							stream << "\"";
							for (long long j = 0; j < x.str_val.size(); ++j) {
								switch (x.str_val[j]) {
								case '\\':
									stream << "\\\\";
									break;
								case '\"':
									stream << "\\\"";
									break;
								case '\n':
									stream << "\\n";
									break;

								default:
									if (isprint(x.str_val[j]))
									{
										stream << x.str_val[j];
									}
									else
									{
										int code = x.str_val[j];
										if (code > 0 && (code < 0x20 || code == 0x7F))
										{
											char buf[] = "\\uDDDD";
											sprintf(buf + 2, "%04X", code);
											stream << buf;
										}
										else {
											stream << x.str_val[j];
										}
									}
								}
							}

							stream << "\"";

							if (x.is_key) {
								stream << " : ";
							}
						}
						else if (x.type == simdjson::internal::tape_type::TRUE_VALUE) {
							stream << "true";
						}
						else if (x.type == simdjson::internal::tape_type::FALSE_VALUE) {
							stream << "false";
						}
						else if (x.type == simdjson::internal::tape_type::DOUBLE) {
							stream << std::fixed << std::setprecision(6) << (x.float_val);
						}
						else if (x.type == simdjson::internal::tape_type::INT64) {
							stream << x.int_val;
						}
						else if (x.type == simdjson::internal::tape_type::UINT64) {
							stream << x.uint_val;
						}
						else if (x.type == simdjson::internal::tape_type::NULL_VALUE) {
							stream << "null ";
						}

						{
							auto& x = ut->get_data_list(i + 1)->value;

							if (
								x.type == simdjson::internal::tape_type::STRING) {
								stream << "\"";
								for (long long j = 0; j < x.str_val.size(); ++j) {
									switch (x.str_val[j]) {
									case '\\':
										stream << "\\\\";
										break;
									case '\"':
										stream << "\\\"";
										break;
									case '\n':
										stream << "\\n";
										break;

									default:
										if (isprint(x.str_val[j]))
										{
											stream << x.str_val[j];
										}
										else
										{
											int code = x.str_val[j];
											if (code > 0 && (code < 0x20 || code == 0x7F))
											{
												char buf[] = "\\uDDDD";
												sprintf(buf + 2, "%04X", code);
												stream << buf;
											}
											else {
												stream << x.str_val[j];
											}
										}
									}
								}

								stream << "\"";

							}
							else if (x.type == simdjson::internal::tape_type::TRUE_VALUE) {
								stream << "true";
							}
							else if (x.type == simdjson::internal::tape_type::FALSE_VALUE) {
								stream << "false";
							}
							else if (x.type == simdjson::internal::tape_type::DOUBLE) {
								stream << std::fixed << std::setprecision(6) << (x.float_val);
							}
							else if (x.type == simdjson::internal::tape_type::INT64) {
								stream << x.int_val;
							}
							else if (x.type == simdjson::internal::tape_type::UINT64) {
								stream << x.uint_val;
							}
							else if (x.type == simdjson::internal::tape_type::NULL_VALUE) {
								stream << "null ";
							}

							++i;
						}
					}
					
					if (i < ut->get_data_size() - 1) {
						stream << ", ";
					}
				}
			}
			else if (ut->is_array()) {
				for (size_t i = 0; i < ut->get_data_size(); ++i) {
					if (ut->get_data_list(i)->is_user_type()) {


						if (((UserType*)ut->get_data_list(i))->is_object()) {
							stream << " { \n";
						}
						else {
							stream << " [ \n";
						}


						_save(stream, (UserType*)ut->get_data_list(i), depth + 1);

						if (((UserType*)ut->get_data_list(i))->is_object()) {
							stream << " } \n";
						}
						else {
							stream << " ] \n";
						}
					}
					else {

						auto& x = ut->get_data_list(i)->value;

						if (
							x.type == simdjson::internal::tape_type::STRING) {
							stream << "\"";
							for (long long j = 0; j < x.str_val.size(); ++j) {
								switch (x.str_val[j]) {
								case '\\':
									stream << "\\\\";
									break;
								case '\"':
									stream << "\\\"";
									break;
								case '\n':
									stream << "\\n";
									break;

								default:
									if (isprint(x.str_val[j]))
									{
										stream << x.str_val[j];
									}
									else
									{
										int code = x.str_val[j];
										if (code > 0 && (code < 0x20 || code == 0x7F))
										{
											char buf[] = "\\uDDDD";
											sprintf(buf + 2, "%04X", code);
											stream << buf;
										}
										else {
											stream << x.str_val[j];
										}
									}
								}
							}

							stream << "\"";
						}
						else if (x.type == simdjson::internal::tape_type::TRUE_VALUE) {
							stream << "true";
						}
						else if (x.type == simdjson::internal::tape_type::FALSE_VALUE) {
							stream << "false";
						}
						else if (x.type == simdjson::internal::tape_type::DOUBLE) {
							stream << std::fixed << std::setprecision(6) << (x.float_val);
						}
						else if (x.type == simdjson::internal::tape_type::INT64) {
							stream << x.int_val;
						}
						else if (x.type == simdjson::internal::tape_type::UINT64) {
							stream << x.uint_val;
						}
						else if (x.type == simdjson::internal::tape_type::NULL_VALUE) {
							stream << "null ";
						}

						
						stream << " ";
					}
					
					if (i < ut->get_data_size() - 1) {
						stream << ", ";
					}
				}
			}
		}

		static void save(const std::string& fileName, class UserType& global) {
			std::ofstream outFile;
			outFile.open(fileName, std::ios::binary); // binary!

			_save(outFile, &global);

			outFile.close();
		}
	};
	
	inline 	claujson::UserType* Parse(const std::string& fileName, int thr_num, UserType* ut, std::vector<Block>& blocks)
	{
		if (thr_num <= 0) {
			thr_num = std::thread::hardware_concurrency();
		}
		if (thr_num <= 0) {
			thr_num = 1;
		}

		claujson::UserType* pool = nullptr;

		int _ = clock();

		{
			simdjson::dom::parser test;

			auto x = test.load(fileName);

			if (x.error() != simdjson::error_code::SUCCESS) {
				std::cout << x.error() << "\n";

				return nullptr;
			}
			if (!test.valid) {
			//	std::cout << "parser is not valid\n";

				//return -2;
			}

			const auto& tape = test.raw_tape();
			const auto& string_buf = test.raw_string_buf();


			std::vector<int64_t> start(thr_num + 1, 0);
			//std::vector<int> key;
			int64_t length;
			int a = clock();

			std::cout << a - _ << "ms\n";


			{
				uint32_t string_length;
				size_t tape_idx = 0;
				uint64_t tape_val = tape[tape_idx];
				uint8_t type = uint8_t(tape_val >> 56);
				uint64_t payload;
				tape_idx++;
				size_t how_many = 0;
				if (type == 'r') {
					how_many = size_t(tape_val & simdjson::internal::JSON_VALUE_MASK);
					length = how_many;

					//key = std::vector<int>(how_many, 0);
				}
				else {
					// Error: no starting root node?
					return nullptr;
				}

				start[0] = 1;
				for (int i = 1; i < thr_num; ++i) {
					start[i] = how_many / thr_num * i;
				}

				int c = clock();
	

				// no l,u,d  any 
				 // true      true
				 // false     true
				/*

				int count = 1;
				for (; tape_idx < how_many; tape_idx++) {
					if (count < thr_num && tape_idx == start[count]) {
						count++; 
					}
					else if (count < thr_num && tape_idx == start[count] + 1) {
						start[count] = tape_idx;
						count++; 
					}

					tape_val = tape[tape_idx];
					payload = tape_val & simdjson::internal::JSON_VALUE_MASK;
					type = uint8_t(tape_val >> 56);

					switch (type) {
					case 'l':
					case 'u':
					case 'd':
						tape_idx++;
						break;
					}
				}
				*/

				/*
				int d = clock();
				std::cout << d - c << "ms\n";
				bool now_object = false;
				bool even = false;

				for (tape_idx = 1; tape_idx < how_many; tape_idx++) {

					//os << tape_idx << " : ";
					tape_val = tape[tape_idx];
					payload = tape_val & simdjson::internal::JSON_VALUE_MASK;
					type = uint8_t(tape_val >> 56);
					
					even = !even;

					switch (type) {
					case '"': // we have a string
						if (now_object && even) {
							key[tape_idx] = 1;
						}

						break;
					case 'l': // we have a long int
					//	if (tape_idx + 1 >= how_many) {
					//		return false;
						//}
						//  os << "integer " << static_cast<int64_t>(tape[++tape_idx]) << "\n";
						++tape_idx;

						break;
					case 'u': // we have a long uint
						//if (tape_idx + 1 >= how_many) {
						//	return false;
						//}
						//  os << "unsigned integer " << tape[++tape_idx] << "\n";
						++tape_idx;
						break;
					case 'd': // we have a double
					  //  os << "float ";
						//if (tape_idx + 1 >= how_many) {
						//	return false;
						//}

						// double answer;
						// std::memcpy(&answer, &tape[++tape_idx], sizeof(answer));
					   //  os << answer << '\n';
						++tape_idx;
						break;
					case 'n': // we have a null
					   // os << "null\n";
						break;
					case 't': // we have a true
					   // os << "true\n";
						break;
					case 'f': // we have a false
					  //  os << "false\n";
						break;
					case '{': // we have an object
					 //   os << "{\t// pointing to next tape location " << uint32_t(payload)
					 //       << " (first node after the scope), "
					  //      << " saturated count "
					   //     << ((payload >> 32) & internal::JSON_COUNT_MASK) << "\n";
						now_object = true; even = false;
						//_stack.push_back(1);
						//_stack2.push_back(0);
						break;
					case '}': // we end an object
					  //  os << "}\t// pointing to previous tape location " << uint32_t(payload)
					  //      << " (start of the scope)\n";
						//_stack.pop_back();
						//_stack2.pop_back();

						now_object = key[uint32_t(payload) - 1] == 1; even = false;
						break;
					case '[': // we start an array
					  //  os << "[\t// pointing to next tape location " << uint32_t(payload)
					  //      << " (first node after the scope), "
					  //      << " saturated count "
					   //     << ((payload >> 32) & internal::JSON_COUNT_MASK) << "\n";
						//_stack.push_back(0);
						now_object = false; even = false;
						break;
					case ']': // we end an array
					 //   os << "]\t// pointing to previous tape location " << uint32_t(payload)
					  //      << " (start of the scope)\n";
						//_stack.pop_back();
						now_object = key[uint32_t(payload) - 1] == 1; even = false;
						break;
					case 'r': // we start and end with the root node
					  // should we be hitting the root node?
						break;
					default:

						return nullptr;
					}
				}
				std::cout << clock() - d << "ms\n";*/
			}
			

			int b = clock();

			std::cout << b - a << "ms\n";

			start[thr_num] = length - 1;

			pool = (claujson::UserType*)calloc(length, sizeof(claujson::UserType));

			claujson::LoadData::parse(pool, *ut, string_buf, tape, length, start, thr_num, blocks); // 0 : use all thread..

			int c = clock();
			std::cout << c - b << "ms\n";
		}
		int c = clock();
		std::cout << c - _ << "ms\n";

		// claujson::LoadData::_save(std::cout, &ut);

		return pool;
	}

	inline int Parse_One(const std::string& str, Data& data) {
		{
			simdjson::dom::parser test;

			auto x = test.parse(str);

			if (x.error() != simdjson::error_code::SUCCESS) {
				std::cout << x.error() << "\n";

				return -1;
			}

			const auto& tape = test.raw_tape();
			const auto& string_buf = test.raw_string_buf();

			data = Convert(&tape[1], string_buf);
		}
		return 0;
	}
}
