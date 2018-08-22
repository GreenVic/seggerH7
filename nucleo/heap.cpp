// heap.cpp
// sram dtcm 128k  0x20000000 0x00020000
// sram axi  512k  0x24000000 0x00080000
// sram 1    128k  0x30000000 0x00020000
// sram 2    128k  0x30020000 0x00020000
// sram 3     32k  0x30040000 0x00008000
// sram 4     64k  0x30080000 0x00010000
// sdRam     128m  0xD0000000 0x08000000
//{{{  includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>

#include "FreeRTOS.h"
#include "task.h"

#include "heap.h"
//}}}

//{{{
class cHeap {
public:
  cHeap (size_t size, bool debug) : mSize(size), mFreeSize(size), mMinFreeSize(size), mDebug(debug) {}

  virtual size_t getSize() { return mSize; }
  virtual size_t getFreeSize() { return mFreeSize; }
  virtual size_t getMinFreeSize() { return mMinFreeSize; }

  virtual uint8_t* alloc (size_t size, const std::string& tag) = 0;
  virtual void free (void* ptr) = 0;

protected:
  const uint32_t mAlignment = 8;
  const uint32_t mAlignmentMask = 7;
  const size_t kHeapStructSize = 8;
  const size_t kHeapMinimumBlockSize = kHeapStructSize << 1;
  const size_t kBlockAllocatedBit = 0x80000000;

  size_t mSize = 0;
  size_t mFreeSize = 0;
  size_t mMinFreeSize = 0;
  bool mDebug = false;
  };
//}}}
//{{{
class cRtosHeap : public cHeap {
public:
  //{{{
  cRtosHeap (uint32_t start, size_t size, bool debug) : cHeap (size, debug) {

    // Ensure the heap starts on a correctly aligned boundary
    size_t uxAddress = (size_t)start;
    mSize = size;
    if ((uxAddress & mAlignmentMask) != 0) {
      uxAddress += (mAlignment - 1);
      uxAddress &= ~((size_t)mAlignmentMask);
      mSize -= uxAddress - (size_t)start;
      }

    uint8_t* pucAlignedHeap = (uint8_t*)uxAddress;

    // mStart is used to hold a pointer to the first item in the list of free blocks.
    mStart.mNextFreeBlock = (tLink_t*)pucAlignedHeap;
    mStart.mBlockSize = (size_t)0;

    // mEnd is used to mark the end of the list of free blocks and is inserted at the end of the heap space. */
    uxAddress = ((size_t)pucAlignedHeap) + mSize;
    uxAddress -= kHeapStructSize;
    uxAddress &= ~((size_t) mAlignmentMask );
    mEnd = (tLink_t*)uxAddress;
    mEnd->mBlockSize = 0;
    mEnd->mNextFreeBlock = NULL;

    // start with single free block sized to take up entire heap space, minus space taken by mEnd
    tLink_t* firstFreeBlock = (tLink_t*)pucAlignedHeap;
    firstFreeBlock->mBlockSize = uxAddress - (size_t)firstFreeBlock;
    firstFreeBlock->mNextFreeBlock = mEnd;

    // Only one block exists - and it covers the entire usable heap space
    mMinFreeSize = firstFreeBlock->mBlockSize;
    mFreeSize = firstFreeBlock->mBlockSize;
    }
  //}}}

  //{{{
  virtual uint8_t* alloc (size_t size, const std::string& tag) {

    size_t largestBlock = 0;

    vTaskSuspendAll();
    uint8_t* allocAddress = allocBlock (size);
    xTaskResumeAll();

    if (mDebug) {
      printf ("cHeap::alloc size:%d free:%d minFree:%d largest:%d\n",
              size, mFreeSize, mMinFreeSize, largestBlock);

      tLink_t* block = mStart.mNextFreeBlock;
      while (block) {
        if ((block->mBlockSize & kBlockAllocatedBit) == 0)
          printf (" - alloc %p size:%d\n", block, block->mBlockSize);
        else
          printf (" -  free %p size:%d\n", block, block->mBlockSize);
        block = block->mNextFreeBlock;
        }
      }

    return allocAddress;
    }
  //}}}
  //{{{
  virtual void free (void* ptr) {

    if (ptr) {
      // memory being freed will have an tLink_t structure immediately before it.
      uint8_t* puc = (uint8_t*)ptr - kHeapStructSize;
      tLink_t* link = (tLink_t*)puc;

      if (mDebug)
        printf ("cHeap::free %p %d\n", ptr, link->mBlockSize & (~kBlockAllocatedBit));

      if (link->mBlockSize & kBlockAllocatedBit) {
        if (link->mNextFreeBlock == NULL) {
          // block is being returned to the heap - it is no longer allocated.
          link->mBlockSize &= ~kBlockAllocatedBit;

          vTaskSuspendAll();
          mFreeSize += link->mBlockSize;
          insertBlockIntoFreeList (link);
          xTaskResumeAll();
          }
        }
      }
    }
  //}}}

private:
  //{{{  struct tLink_t
  typedef struct A_BLOCK_LINK {
    struct A_BLOCK_LINK* mNextFreeBlock; // The next free block in the list
    size_t mBlockSize;                   // The size of the free block
    } tLink_t;
  //}}}

  //{{{
  uint8_t* allocBlock (size_t size) {

    uint8_t* allocAddress = NULL;
    size_t largestBlock = 0;

    // The wanted size is increased so it can contain a tLink_t structure in addition to the requested amount of bytes
    size += kHeapStructSize;
    // Ensure that blocks are always aligned to the required number of bytes
    if ((size & mAlignmentMask) != 0x00)
      // Byte alignment required. */
      size += (mAlignment - (size & mAlignmentMask));

    if ((size > 0) && (size <= mFreeSize)) {
      // Traverse the list from the start (lowest address) block until one of adequate size is found
      tLink_t* prevBlock = &mStart;
      tLink_t* block = mStart.mNextFreeBlock;
      largestBlock = mStart.mBlockSize;
      while ((block->mBlockSize < size) && (block->mNextFreeBlock != NULL)) {
        prevBlock = block;
        block = block->mNextFreeBlock;
        if (block->mBlockSize > largestBlock)
          largestBlock = block->mBlockSize;
        }

      // If the end marker was reached then a block of adequate size was not found
      if (block != mEnd) {
        // Return the memory space pointed to - jumping over the tLink_t structure at its start
        allocAddress = ((uint8_t*)prevBlock->mNextFreeBlock) + kHeapStructSize;

        //This block is being returned for use so must be taken out of the list of free blocks
        prevBlock->mNextFreeBlock = block->mNextFreeBlock;

        // If the block is larger than required it can be split into two.
        if ((block->mBlockSize - size) > kHeapMinimumBlockSize) {
          // This block is to be split into two
          // Create a new block following the number of bytes requested
          tLink_t* newLink = (tLink_t*)(((uint8_t*)block) + size);

          // Calculate the sizes of two blocks split from the single block
          newLink->mBlockSize = block->mBlockSize - size;
          block->mBlockSize = size;

          // Insert the new block into the list of free blocks
          insertBlockIntoFreeList (newLink);
          }

        mFreeSize -= block->mBlockSize;
        if (mFreeSize < mMinFreeSize)
          mMinFreeSize = mFreeSize;

        // The block is being returned - it is allocated and owned by the application and has no "next" block. */
        block->mBlockSize |= kBlockAllocatedBit;
        block->mNextFreeBlock = NULL;
        }
      }

    return allocAddress;
    }
  //}}}
  //{{{
  void insertBlockIntoFreeList (tLink_t* insertBlock) {

    // iterate through list until block found that has higher address than block being inserted
    tLink_t* blockIt;
    for (blockIt = &mStart; blockIt->mNextFreeBlock < insertBlock; blockIt = blockIt->mNextFreeBlock) {}

    // Do the block being inserted, and the block it is being inserted after make a contiguous block of memory? */
    uint8_t* puc = (uint8_t*)blockIt;
    if ((puc + blockIt->mBlockSize) == (uint8_t*)insertBlock) {
      blockIt->mBlockSize += insertBlock->mBlockSize;
      insertBlock = blockIt;
      }

    // Do the block being inserted, and the block it is being inserted before make a contiguous block of memory?
    puc = (uint8_t*)insertBlock;
    if ((puc + insertBlock->mBlockSize) == (uint8_t*)blockIt->mNextFreeBlock) {
      if (blockIt->mNextFreeBlock != mEnd ) {
        // Form one big block from the two blocks
        insertBlock->mBlockSize += blockIt->mNextFreeBlock->mBlockSize;
        insertBlock->mNextFreeBlock = blockIt->mNextFreeBlock->mNextFreeBlock;
        }
      else
        insertBlock->mNextFreeBlock = mEnd;
      }
    else
      insertBlock->mNextFreeBlock = blockIt->mNextFreeBlock;

    // If the block being inserted plugged a gab, so was merged with the block
    // before and the block after, then it's mNextFreeBlock pointer will have
    // already been set, and should not be set here as that would make it point to itself/
    if (blockIt != insertBlock)
      blockIt->mNextFreeBlock = insertBlock;
    }
  //}}}

  tLink_t mStart;
  tLink_t* mEnd = NULL;
  };
//}}}
//{{{
class cSdRamHeap : public cHeap {
// simple slow heap for unreliable sdRam
public:
  cSdRamHeap (uint8_t* start, size_t size, bool debug) : cHeap(size, debug), mStart(start) {}

  //{{{
  virtual uint8_t* alloc (size_t size, const std::string& tag) {

    uint8_t* allocAddress = nullptr;

    vTaskSuspendAll();

    if (mBlockMap.empty()) {
      mBlockMap.insert (std::map<uint8_t*,cBlock*>::value_type (mStart, new cBlock (mStart, size, true, tag)));
      mBlockMap.insert (std::map<uint8_t*,cBlock*>::value_type (mStart+size, new cBlock (mStart+size, mSize-size, false, "free")));
      mFreeSize -= size;
      if (mFreeSize < mMinFreeSize)
        mMinFreeSize = mFreeSize;
      allocAddress = mStart;
      }
    else {
      for (auto block : mBlockMap) {
        if ((!block.second->mAllocated) && (size <= block.second->mSize)) {
          block.second->mAllocated = true;
          if (size < block.second->mSize) {
            printf ("cSdRamHeap::alloc - split block %x:%x\n", size, block.second->mSize);
            auto blockit = mBlockMap.insert (
              std::map<uint8_t*,cBlock*>::value_type (
                block.second->mAddress+size, new cBlock (block.second->mAddress+size,
                block.second->mSize-size, false, "free")));
            block.second->mSize = size;
            }
          else
            printf ("cSdRamHeap::alloc - reallocate free block %x\n", size);
          block.second->mTag = tag;

          mFreeSize -= size;
          if (mFreeSize < mMinFreeSize)
            mMinFreeSize = mFreeSize;

          allocAddress = block.second->mAddress;
          break;
          }
        }
      }

    if (mDebug) {
      printf ("cSdRamHeap::alloc %p %x\n", allocAddress, size);
      list();
      }

    xTaskResumeAll();

    return allocAddress;
    }
  //}}}
  //{{{
  virtual void free (void* ptr) {

    if (ptr) {
      if (mDebug)
        printf ("cSdRamHeap::free %p\n", ptr);

      vTaskSuspendAll();
      auto blockIt = mBlockMap.find ((uint8_t*)ptr);
      if (blockIt == mBlockMap.end())
        printf ("cSdRamHeap::free **** free block not found\n");
      else if (!blockIt->second->mAllocated)
        printf ("cSdRamHeap::free **** deallocating free blcok\n");
      else {
        if (mDebug)
          printf ("cSdRamHeap::free block found\n");

        // free block
        blockIt->second->mAllocated = false;
        blockIt->second->mTag = "free";
        mFreeSize += blockIt->second->mSize;
        auto nextBlockIt = blockIt++;
        if (nextBlockIt != mBlockMap.end()) {
          printf ("have next \n");
          if (!nextBlockIt->second->mAllocated) {
            printf ("should remove next block\n");
            }
          }

        if (blockIt != mBlockMap.begin()) {
          auto prevBlockIt = blockIt--;
          printf ("have prev \n");
          if (!prevBlockIt->second->mAllocated) {
            printf ("should remove prev block\n");
            }
          }
        }

      if (mDebug)
        list();

      xTaskResumeAll();
      }
    }
  //}}}

private:
  //{{{
  class cBlock {
  public:
    cBlock (uint8_t* address, uint32_t size, bool allocated, const std::string& tag) :
      mAddress(address), mSize(size), mAllocated(allocated), mTag(tag) {}

    uint8_t* mAddress = nullptr;
    uint32_t mSize = 0;
    bool mAllocated = false;
    std::string mTag;
    };
  //}}}
  //{{{
  void list() {
    for (auto block : mBlockMap)
      printf ("block %p:%p %7x %c %s\n",
              block.first, block.second->mAddress, block.second->mSize,
              block.second->mAllocated ? 'a' : 'f', block.second->mTag.c_str());
    printf ("-------------------------\n");
    }
  //}}}

  uint8_t* mStart = nullptr;
  std::map <uint8_t*, cBlock*> mBlockMap;
  };
//}}}

// dtcm
cRtosHeap* mDtcmHeap = nullptr;
//{{{
uint8_t* dtcmAlloc (size_t size) {
  if (!mDtcmHeap)
    mDtcmHeap = new cRtosHeap (0x20000000, 0x00020000, false);
  return (uint8_t*)mDtcmHeap->alloc (size, "");
  }
//}}}
void dtcmFree (void* ptr) { mDtcmHeap->free (ptr); }
size_t getDtcmSize(){ return mDtcmHeap ? mDtcmHeap->getSize() : 0 ; }
size_t getDtcmFreeSize() { return mDtcmHeap ? mDtcmHeap->getFreeSize() : 0 ; }
size_t getDtcmMinFreeSize() { return mDtcmHeap ? mDtcmHeap->getMinFreeSize() : 0 ; }

// sram AXI
cRtosHeap* mSramHeap = nullptr;
//{{{
void* pvPortMalloc (size_t size) {

  if (!mSramHeap)
    mSramHeap = new cRtosHeap (0x24010000, 0x00070000, false);

  void* allocAddress = mSramHeap->alloc (size, "");
  if (allocAddress) {
    //printf ("pvPortMalloc %p %d\n", allocAddress, size);
    }
  else
    printf ("pvPortMalloc %d fail\n", size);

  return allocAddress;
  }
//}}}
//{{{
void vPortFree (void* ptr) {

  //printf ("vPortFree %p\n", ptr);
   mSramHeap->free (ptr);
  }
//}}}
size_t getSramSize() { return mSramHeap ? mSramHeap->getSize() : 0 ; }
size_t getSramFreeSize() { return mSramHeap ? mSramHeap->getFreeSize() : 0 ; }
size_t getSramMinFreeSize() { return mSramHeap ? mSramHeap->getMinFreeSize() : 0 ; }

//{{{
//void* operator new (size_t size) {

  //void* allocAddress = malloc (size);
  //printf ("new %p %d\n", allocAddress, size);
  //return allocAddress;
  //}
//}}}
//{{{
//void operator delete (void* ptr) {

  //printf ("free %p\n", ptr);
  //free (ptr);
  //}
//}}}

// sram 123
cRtosHeap* mSram123Heap = nullptr;
//{{{
uint8_t* sram123Alloc (size_t size) {
  if (!mSram123Heap)
    mSram123Heap = new cRtosHeap (0x30000000, 0x00048000, false);
  return (uint8_t*)mSram123Heap->alloc (size, "");
  }
//}}}
void sram123Free (void* ptr) { mSram123Heap->free (ptr); }
size_t getSram123Size(){ return mSram123Heap ? mSram123Heap->getSize() : 0 ; }
size_t getSram123FreeSize() { return mSram123Heap ? mSram123Heap->getFreeSize() : 0 ; }
size_t getSram123MinFreeSize() { return mSram123Heap ? mSram123Heap->getMinFreeSize() : 0 ; }

// sd ram
cSdRamHeap* mSdRamHeap = nullptr;
//{{{
uint8_t* sdRamAlloc (size_t size, const std::string& tag) {

  if (!mSdRamHeap)
    mSdRamHeap = new cSdRamHeap ((uint8_t*)0xD0000000, 0x08000000, true);

  return mSdRamHeap->alloc (size, tag);
  }
//}}}
void sdRamFree (void* ptr) { mSdRamHeap->free (ptr); }
size_t getSdRamSize() { return mSdRamHeap ? mSdRamHeap->getSize() : 0; }
size_t getSdRamFreeSize() { return mSdRamHeap ? mSdRamHeap->getFreeSize() : 0; }
size_t getSdRamMinFreeSize() { return mSdRamHeap ? mSdRamHeap->getMinFreeSize() : 0; }
