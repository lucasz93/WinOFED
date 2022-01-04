#define HASH_SIZE   1024

template <typename KeyType>
class HashVal{
public:
	HashVal(){}; //TODO -remove this default constructor by making it private !!!!!
	HashVal( KeyType keyType ): m_Next ( NULL ), m_keyType( keyType) {};
    void                *m_Next;
    typename KeyType    m_keyType;
};

class HashIterator {
public:
    int     m_iterIndex;
    void*   m_iterCurrent;

    HashIterator(): m_iterIndex(0){}
    HashIterator(void* curr): m_iterIndex(0), m_iterCurrent(curr){}

    void InitIterator(void* start) {
        m_iterIndex = 0;
        m_iterCurrent = start;
    }
};

template <class HashFunction, class HashVal> class HashTable{

 public:
    NTSTATUS Init(int HashValOffset, HashFunction func, int hashSize = HASH_SIZE) {
        m_hashSize = hashSize;
        m_Objects = new("HashTable Init ") void* [m_hashSize];

        if(m_Objects == NULL) {
            return STATUS_NO_MEMORY;
        }

        m_offset = HashValOffset;
        m_hashFunction = func;
        
        for(int i = 0; i < m_hashSize; i++) {
            m_Objects[i] = NULL;
        }

        return STATUS_SUCCESS;
    }

    ~HashTable<HashFunction, HashVal>() {   
        delete m_Objects;
    }

    void Insert(void * Data) {
         HashVal* hashVal = GetHashVal(Data);
         int index =  m_hashFunction(hashVal->m_keyType) % m_hashSize;
         
#if DBG
         CEndPoint* p_TempEndPoint = (CEndPoint*)Get(hashVal);
         ASSERT(p_TempEndPoint == NULL);
#endif
         
         if (m_Objects[index] == NULL) {
            m_Objects[index] = Data;
            hashVal->m_Next = NULL;
            return;
         }

         void* curr_list = m_Objects[index];
         m_Objects[index] = Data;
         hashVal->m_Next = curr_list;
    }

    void* Get(HashVal* hashVal) {
         int index =  m_hashFunction(hashVal->m_keyType) % m_hashSize;

         void* currData = m_Objects[index];
         while ( currData ) {
             HashVal* currHash = GetHashVal(currData);
             if (currHash->m_keyType == hashVal->m_keyType) {
                return currData;
             }
             currData = currHash->m_Next;
         }      

         return NULL;
    }

    void Delete(HashVal* hashVal) {
         int index =  m_hashFunction(hashVal->m_keyType) % m_hashSize;

         void* currData = m_Objects[index];
         HashVal* prevHash = NULL;
         while ( currData ) {
             HashVal* currHash = GetHashVal(currData);  
             if (currHash->m_keyType == hashVal->m_keyType) {
                if (prevHash) {
                    prevHash->m_Next = currHash->m_Next;    
                }
                else {
                    m_Objects[index] = currHash->m_Next; //The element we found is the first one
                }
                return;
             }
             currData = currHash->m_Next;
             prevHash = currHash;
         }      

         return;
    }

    void* GetHashStart() {
        return m_Objects[0];
    }

    void* GetNext(HashIterator* iter) {
        void* currElement = NULL;

        if(iter == NULL)
        {
            return NULL;
        }
        
        if (iter->m_iterCurrent){
            currElement = iter->m_iterCurrent;  
        }
        else {
            while ((!iter->m_iterCurrent) && (iter->m_iterIndex < m_hashSize - 1)) {
                iter->m_iterIndex++;
                iter->m_iterCurrent = m_Objects[iter->m_iterIndex];
            }

            if (iter->m_iterCurrent) {
                currElement = iter->m_iterCurrent;
            }
        }

        if (currElement) {
            HashVal* currHash = GetHashVal(currElement);
            iter->m_iterCurrent = currHash->m_Next;
            return currElement;
        }

        return NULL; //No more elements found

    }

    
    
 private:
    int m_hashSize;
    void **m_Objects;
    HashFunction m_hashFunction;
    int m_offset;   //in bytes  


    HashVal* GetHashVal(void* Data){
        return (HashVal*)((char*)Data + m_offset);
    }

    void PrintIndex(int index){
        void* test_curr = m_Objects[index];
         printf(">>******\nIndex %d contains:\n", index);
         while ( test_curr ) {
             HashVal* curr_hash = (HashVal*)((char*)test_curr + m_offset);  
             printf("curr data: %p key: %d\n", test_curr, curr_hash->keyType);
             test_curr = curr_hash->Next;
         }
         printf("******<<\n");
    }
 
};


