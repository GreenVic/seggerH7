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
  //{{{
  cHeap (uint32_t start, size_t size, bool debug) : mDebug (debug) {

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
    mMinFreeBytesRemaining = firstFreeBlock->mBlockSize;
    mFreeBytesRemaining = firstFreeBlock->mBlockSize;
    }
  //}}}

  size_t getSize() { return mSize; }
  size_t getFree() { return mFreeBytesRemaining; }
  size_t getMinSize() { return mMinFreeBytesRemaining; }

  //{{{
  uint8_t* alloc (size_t size) {

    size_t largestBlock = 0;

    vTaskSuspendAll();
    uint8_t* allocAddress = allocBlock (size);
    xTaskResumeAll();

    if (mDebug) {
      printf ("cHeap::alloc size:%d free:%d minFree:%d largest:%d\n",
              size, mFreeBytesRemaining,mMinFreeBytesRemaining, largestBlock);

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
  void free (void* ptr) {

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
          mFreeBytesRemaining += link->mBlockSize;
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

    if ((size > 0) && (size <= mFreeBytesRemaining)) {
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

        mFreeBytesRemaining -= block->mBlockSize;
        if (mFreeBytesRemaining < mMinFreeBytesRemaining )
          mMinFreeBytesRemaining = mFreeBytesRemaining;

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

  const uint32_t mAlignment = 8;
  const uint32_t mAlignmentMask = 7;
  const size_t kHeapStructSize = 8;
  const size_t kHeapMinimumBlockSize = kHeapStructSize << 1;
  const size_t kBlockAllocatedBit = 0x80000000;

  tLink_t mStart;
  tLink_t* mEnd = NULL;
  size_t mSize = 0;
  size_t mFreeBytesRemaining = 0;
  size_t mMinFreeBytesRemaining = 0;
  bool mDebug = false;
  };
//}}}
//{{{
class cSdRamHeap {
public:
  //{{{
  cSdRamHeap (uint8_t* start, size_t size, bool debug)
    : mStart(start), mSize(size), mFreeSize(mSize), mMinFreeSize(mSize), mDebug(debug) {}
  //}}}

  size_t getSize() { return mSize; }
  size_t getFree() { return mFreeSize; }
  size_t getMinFree() { return mMinFreeSize; }

  //{{{
  uint8_t* alloc (size_t size) {

    uint8_t* allocAddress = nullptr;

    vTaskSuspendAll();

    if (mBlockMap.empty()) {
      mBlockMap.insert (std::map<uint8_t*,cBlock*>::value_type (mStart, new cBlock (mStart, size, true)));
      mBlockMap.insert (std::map<uint8_t*,cBlock*>::value_type (mStart+size, new cBlock (mStart+size, mSize-size, false)));
      mFreeSize -= size;
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
                block.second->mSize-size, false)));
            block.second->mSize = size;
            }
          else
            printf ("cSdRamHeap::alloc - reallocate free block %x\n", size);

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
  void free (void* ptr) {

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
        blockIt->second->mAllocated = false;
        mFreeSize += blockIt->second->mSize;
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
    cBlock (uint8_t* address, uint32_t size, bool allocated) :
      mAddress(address), mSize(size), mAllocated(allocated) {}

    uint8_t* mAddress = nullptr;
    uint32_t mSize = 0;
    bool mAllocated = false;
    };
  //}}}
  //{{{
  void list() {
    for (auto block : mBlockMap)
      printf ("block %p %p %x %d\n",
              block.first, block.second->mAddress, block.second->mSize, block.second->mAllocated);
    printf ("-------------------------\n");
    }
  //}}}

  std::map<uint8_t*, cBlock*> mBlockMap;
  uint8_t* mStart = nullptr;
  size_t mSize = 0;
  size_t mFreeSize = 0;
  size_t mMinFreeSize = 0;
  bool mDebug = false;
  };
//}}}

// dtcm
cHeap* mDtcmHeap = nullptr;
//{{{
uint8_t* dtcmAlloc (size_t size) {
  if (!mDtcmHeap)
    mDtcmHeap = new cHeap (0x20000000, 0x00020000, false);
  return (uint8_t*)mDtcmHeap->alloc (size);
  }
//}}}
void dtcmFree (void* ptr) { mDtcmHeap->free (ptr); }
size_t getDtcmSize(){ return mDtcmHeap ? mDtcmHeap->getSize() : 0 ; }
size_t getDtcmFree() { return mDtcmHeap ? mDtcmHeap->getFree() : 0 ; }
size_t getDtcmMinFree() { return mDtcmHeap ? mDtcmHeap->getMinSize() : 0 ; }

// sram AXI
cHeap* mSramHeap = nullptr;
//{{{
void* pvPortMalloc (size_t size) {

  if (!mSramHeap)
    mSramHeap = new cHeap (0x24010000, 0x00070000, false);

  void* allocAddress = mSramHeap->alloc (size);
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
size_t getSramFree() { return mSramHeap ? mSramHeap->getFree() : 0 ; }
size_t getSramMinFree() { return mSramHeap ? mSramHeap->getMinSize() : 0 ; }

//{{{
//void* operator new (size_t size) {

  //void* allocAddress = malloc (size);
  ////printf ("new %p %d\n", allocAddress, size);
  //return allocAddress;
  //}
//}}}
//{{{
//void operator delete (void* ptr) {

  ////printf ("free %p\n", ptr);
  //free (ptr);
  //}
//}}}

// sram 123
cHeap* mSram123Heap = nullptr;
//{{{
uint8_t* sram123Alloc (size_t size) {
  if (!mSram123Heap)
    mSram123Heap = new cHeap (0x30000000, 0x00048000, false);
  return (uint8_t*)mSram123Heap->alloc (size);
  }
//}}}
void sram123Free (void* ptr) { mSram123Heap->free (ptr); }
size_t getSram123Size(){ return mSram123Heap ? mSram123Heap->getSize() : 0 ; }
size_t getSram123Free() { return mSram123Heap ? mSram123Heap->getFree() : 0 ; }
size_t getSram123MinFree() { return mSram123Heap ? mSram123Heap->getMinSize() : 0 ; }

// sd ram
cSdRamHeap* mSdramHeap = nullptr;
//{{{
uint8_t* sdRamAlloc (size_t size) {

  if (!mSdramHeap)
    mSdramHeap = new cSdRamHeap ((uint8_t*)0xD0000000, 0x08000000, true);

  return mSdramHeap->alloc (size);
  }
//}}}
//{{{
void sdRamFree (void* ptr) {
  mSdramHeap->free (ptr);
  }
//}}}
size_t getSdRamSize() { return mSdramHeap ? mSdramHeap->getSize() : 0; }
size_t getSdRamFree() { return mSdramHeap ? mSdramHeap->getFree() : 0; }
size_t getSdRamMinFree() { return mSdramHeap ? mSdramHeap->getMinFree() : 0; }
