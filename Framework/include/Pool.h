#pragma once
#include <mutex>
#include <functional>
#include <cassert>

template<typename T>
class Pool
{
public:
	Pool()
	: m_pBuffer(nullptr),
	m_pActive(nullptr),
	m_pFree(nullptr),
	m_Capacity(0),
	m_Count(0)
	{
	}

	~Pool()
	{
		Term();
	}

	bool Init(uint32_t count)
	{
		std::lock_guard<std::mutex> guard(m_Mutex);

		m_pBuffer = static_cast<uint8_t*>(malloc(sizeof(Item) * (count + 2)));
		if (m_pBuffer == nullptr)
		{
			return false;
		}

		m_Capacity = count;

		for (uint32_t i = 0u; i < m_Capacity; i++)
		{
			Item* item = GetItem(i + 2);
			item->m_Index = i;
		}

		m_pActive = GetItem(0);
		m_pActive->m_pPrev = m_pActive->m_pNext = m_pActive;
		m_pActive->m_Index = uint32_t(-1);

		m_pFree = GetItem(1);
		m_pFree->m_Index = uint32_t(-2);

		for (uint32_t i = 1u; i < m_Capacity + 2; i++)
		{
			GetItem(i)->m_pPrev = nullptr;
			GetItem(i)->m_pNext = GetItem(i + 1); // m_pFree->m_pNext��m_Index==2�̃A�C�e���ɂȂ�B���ׂ�m_pNext��ʂ���m_pFree�ɂȂ����Ă���`�B�t���[���X�g��m_pNext�݂̂łȂ����Ă���Bm_pPrev������̂̓A�N�e�B�u���X�g�ɓ����Ă���B
		}

		GetItem(m_Capacity + 1)->m_pPrev = m_pFree; // �z��̍Ō�̗v�f

		m_Count = 0;

		return true;
	}

	void Term()
	{
		std::lock_guard<std::mutex> guard(m_Mutex);

		if (m_pBuffer != nullptr)
		{
			free(m_pBuffer);
			m_pBuffer = nullptr;
		}

		m_pActive = nullptr;
		m_pFree = nullptr;
		m_Capacity = 0;
		m_Count = 0;
	}

	T* Alloc(std::function<void(uint32_t, T*)> func = nullptr)
	{
		std::lock_guard<std::mutex> guard(m_Mutex);

		if (m_pFree->m_pNext == m_pFree || m_Count >= m_Capacity)
		{
			return nullptr;
		}

		// �t���[���X�g�̕���m_pFree����m_pNext��ʂ��ĂȂ����Ă���Ƃ������R�Ȍ`�Ȃ̂����A�Ȃ����A�N�e�B�u���X�g��m_pPrev�̕��ɂ����̂ڂ�`�ŘA�����Ă���Ƃ�����̎���

		// �t���[���X�g�̐擪����Ƃ��Ă��ăA�N�e�B�u���X�g�̐擪�ɑ}������
		Item* item = m_pFree->m_pNext;
		m_pFree->m_pNext = item->m_pNext;

		item->m_pPrev = m_pActive->m_pPrev;
		item->m_pNext = m_pActive;
		item->m_pPrev->m_pNext = item->m_pNext->m_pPrev = item;

		m_Count++;

		T* val = new ((void*)item) T();

		if (func != nullptr)
		{
			func(item->m_Index, val);
		}

		return val;
	}

	void Free(T* pValue)
	{
		if (pValue == nullptr)
		{
			return;
		}

		std::lock_guard<std::mutex> guard(m_Mutex);

		// �t���[���X�g�̐擪�ɑ}������
		Item* item = reinterpret_cast<Item*>(pValue);
		item->m_pPrev->m_pNext = item->m_pNext;
		item->m_pNext->m_pPrev = item->m_pPrev;

		item->m_pPrev = nullptr;
		item->m_pNext = m_pFree->m_pNext;

		m_pFree->m_pNext = item;

		m_Count--;
	}

	uint32_t GetSize() const
	{
		return m_Capacity;
	}

	uint32_t GetUsedCount() const
	{
		return m_Count;
	}

	uint32_t GetAvailableCount() const
	{
		return m_Capacity - m_Count;
	}

private:
	struct Item
	{
		T m_Value;
		uint32_t m_Index;
		Item* m_pPrev;
		Item* m_pNext;

		Item()
		: m_Value()
		, m_Index(0)
		, m_pPrev(nullptr)
		, m_pNext(nullptr)
		{
		}

		~Item() {}
	};

	uint8_t* m_pBuffer;
	Item* m_pActive;
	Item* m_pFree;
	uint32_t m_Capacity;
	uint32_t m_Count;
	std::mutex m_Mutex;

	Item* GetItem(uint32_t index)
	{
		assert(0 <= index && index <= m_Capacity + 2);
		return reinterpret_cast<Item*>(m_pBuffer + sizeof(Item) * index);
	}

	Pool(const Pool&) = delete;
	void operator=(const Pool&) = delete;
};
