// heap.cpp
//{{{  includes
#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "heap.h"
//}}}

// 0x20000000 0x00020000  sram DTCM 128k
// .
// 0x24000000 0x00080000  sram axi  512k
// .
// 0x30000000 0x00020000  sram1  128k
// 0x30020000 0x00020000  sram2  128k
// 0x30040000 0x00008000  sram3  32k
// .
// 0x30080000 0x00010000  sram4  64k

//{{{
class cHeap {
public:
  //{{{
  cHeap (uint32_t start, size_t size) {

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
  size_t getFreeSize() { return mFreeBytesRemaining; }
  size_t getMinSize() { return mMinFreeBytesRemaining; }

  //{{{
  uint8_t* allocInt (size_t size) {

    uint8_t* allocAddress = NULL;
    size_t largestBlock = 0;

      {
      // The wanted size is increased so it can contain a tLink_t structure in addition to the requested amount of bytes
      size += kHeapStructSize;

      // Ensure that blocks are always aligned to the required number of bytes
      if ((size & mAlignmentMask) != 0x00)
        // Byte alignment required. */
        size += (mAlignment - (size & mAlignmentMask));
      }

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
  uint8_t* alloc (size_t size) {

    size_t largestBlock = 0;

    vTaskSuspendAll();
    uint8_t* allocAddress = allocInt (size);
    xTaskResumeAll();

    if (allocAddress) {
      //printf ("heap alloc %p size:%d free:%d minFree:%d\n",
      //        allocAddress, size, mFreeBytesRemaining,mMinFreeBytesRemaining);
      }
    else {
      printf ("***heap alloc fail size:%d free:%d minFree:%d largest:%d\n",
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

    //printf ("sdRamFree %p\n", p);

    if (ptr) {
      // memory being freed will have an tLink_t structure immediately before it.
      uint8_t* puc = (uint8_t*)ptr - kHeapStructSize;
      tLink_t* link = (tLink_t*)puc;

      if ((link->mBlockSize & kBlockAllocatedBit ) != 0) {
        if (link->mNextFreeBlock == NULL) {
          // block is being returned to the heap - it is no longer allocated.
          link->mBlockSize &= ~kBlockAllocatedBit;

          vTaskSuspendAll();
            {
            // Add this block to the list of free blocks.
            mFreeBytesRemaining += link->mBlockSize;
            insertBlockIntoFreeList (link);
            }
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
  };
//}}}

// sram AXI
cHeap* mSramHeap = nullptr;

//{{{
uint8_t* sramAlloc (size_t size) {
  if (!mSramHeap)
    mSramHeap = new cHeap (0x24010000, 0x00070000);
  return (uint8_t*)mSramHeap->alloc (size);
  }
//}}}
void sramFree (void* ptr) { mSramHeap->free (ptr); }
size_t getSramSize() { return mSramHeap->getSize(); }
size_t getSramFreeSize() { return mSramHeap->getFreeSize(); }
size_t getSramMinFreeSize() { return mSramHeap->getMinSize(); }

//{{{
void* pvPortMalloc (size_t size) {

  void* allocAddress = sramAlloc (size);
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
  sramFree (ptr);
  }
//}}}

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

// DTCM
cHeap* mDtcmHeap = nullptr;
//{{{
uint8_t* dtcmAlloc (size_t size) {
  if (!mDtcmHeap)
    mDtcmHeap = new cHeap (0x20000000, 0x00020000);
  return (uint8_t*)mDtcmHeap->alloc (size);
  }
//}}}
void dtcmFree (void* ptr) { mDtcmHeap->free (ptr); }
size_t getDtcmSize(){ return mDtcmHeap->getSize(); }
size_t getDtcmFreeSize() { return mDtcmHeap->getFreeSize(); }
size_t getDtcmMinFreeSize() { return mDtcmHeap->getMinSize(); }

// sram 123
cHeap* mSram123Heap = nullptr;
//{{{
uint8_t* sram123Alloc (size_t size) {
  if (!mSram123Heap)
    mSram123Heap = new cHeap (0x30000000, 0x00048000);
  return (uint8_t*)mSram123Heap->alloc (size);
  }
//}}}
void sram123Free (void* ptr) { mSram123Heap->free (ptr); }
size_t getSram123Size(){ return mSram123Heap->getSize(); }
size_t getSram123FreeSize() { return mSram123Heap->getFreeSize(); }
size_t getSram123MinFreeSize() { return mSram123Heap->getMinSize(); }

// sd ram
#define SDRAM_DEVICE_ADDR 0xD0000000
#define SDRAM_DEVICE_SIZE 0x08000000
#define LCD_WIDTH  1024
#define LCD_HEIGHT 600
cHeap* mSdRamHeap = nullptr;
//{{{
uint8_t* sdRamAllocInt (size_t size) {
  if (!mSdRamHeap)
    mSdRamHeap = new cHeap (SDRAM_DEVICE_ADDR + LCD_WIDTH*LCD_HEIGHT*4,  SDRAM_DEVICE_SIZE - LCD_WIDTH*LCD_HEIGHT*4);
  return mSdRamHeap->allocInt (size);
  }
//}}}
//{{{
uint8_t* sdRamAlloc (size_t size) {
  if (!mSdRamHeap)
    mSdRamHeap = new cHeap (SDRAM_DEVICE_ADDR + LCD_WIDTH*LCD_HEIGHT*4,  SDRAM_DEVICE_SIZE - LCD_WIDTH*LCD_HEIGHT*4);
  return mSdRamHeap->alloc (size);
  }
//}}}
void sdRamFree (void* ptr) { mSdRamHeap->free (ptr); }
size_t getSdRamSize() { return mSdRamHeap->getSize(); }
size_t getSdRamFreeSize() { return mSdRamHeap->getFreeSize(); }
size_t getSdRamMinFreeSize() { return mSdRamHeap->getMinSize(); }
