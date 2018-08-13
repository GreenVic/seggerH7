#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "heap.h"

// 0x20000000 0x00020000  sram DTCM 128k
// ...
// 0x24000000 0x00080000  sram axi  512k
// ...
// 0x30000000 0x00020000  sram1  128k
// 0x30020000 0x00020000  sram2  128k
// 0x30040000 0x00008000  sram3  32k
// ....
// 0x30080000 0x00010000  sram4  64k

#define DTCM_ADDR 0x20000000
#define DTCM_SIZE 0x00020000
static uint8_t* mDtcmAlloc = (uint8_t*)DTCM_ADDR;
//{{{
uint8_t* dtcmAlloc (size_t bytes) {

  vTaskSuspendAll();

  uint8_t* alloc = mDtcmAlloc;
  if (alloc + bytes <= (uint8_t*)DTCM_ADDR + DTCM_SIZE)
    mDtcmAlloc += bytes;
  else
    alloc = NULL;

  xTaskResumeAll();

  return alloc;
  }
//}}}

#define SRAM123_ADDR 0x30000000
#define SRAM123_SIZE 0x00048000
static uint8_t* mSram123Alloc = (uint8_t*)SRAM123_ADDR;
//{{{
uint8_t* sram123Alloc (size_t bytes) {

  vTaskSuspendAll();

  uint8_t* alloc = mSram123Alloc;
  if (alloc + bytes <= (uint8_t*)SRAM123_ADDR + SRAM123_SIZE)
    mSram123Alloc += bytes;
  else
    alloc = NULL;

  xTaskResumeAll();

  return alloc;
  }
//}}}

//{{{
void* pvPortMalloc (size_t size) {
  vTaskSuspendAll();
  void* allocAddress = malloc (size);
  xTaskResumeAll();
  return allocAddress;
  }
//}}}
//{{{
void vPortFree (void* pv) {
  if (pv != NULL) {
    vTaskSuspendAll();
    free (pv);
    xTaskResumeAll();
    }
  }
//}}}

//{{{
class cHeap {
public:
  //{{{
  void init (uint32_t start, size_t size) {

    tLink_t* pxFirstFreeBlock;
    uint8_t* pucAlignedHeap;

    // Ensure the heap starts on a correctly aligned boundary
    size_t uxAddress = (size_t)start;
    size_t xTotalHeapSize = size;
    if ((uxAddress & portBYTE_ALIGNMENT_MASK) != 0) {
      uxAddress += (portBYTE_ALIGNMENT - 1);
      uxAddress &= ~((size_t)portBYTE_ALIGNMENT_MASK);
      xTotalHeapSize -= uxAddress - (size_t)start;
      }

    pucAlignedHeap = (uint8_t*)uxAddress;

    // mStart is used to hold a pointer to the first item in the list of free blocks.
    mStart.mNextFreeBlock = (tLink_t*)pucAlignedHeap;
    mStart.mBlockSize = (size_t)0;

    // mEnd is used to mark the end of the list of free blocks and is inserted at the end of the heap space. */
    uxAddress = ((size_t)pucAlignedHeap) + xTotalHeapSize;
    uxAddress -= kHeapStructSize;
    uxAddress &= ~((size_t) portBYTE_ALIGNMENT_MASK );
    mEnd = (tLink_t*)uxAddress;
    mEnd->mBlockSize = 0;
    mEnd->mNextFreeBlock = NULL;

    // start with single free block sized to take up entire heap space, minus space taken by mEnd
    pxFirstFreeBlock = (tLink_t*)pucAlignedHeap;
    pxFirstFreeBlock->mBlockSize = uxAddress - (size_t)pxFirstFreeBlock;
    pxFirstFreeBlock->mNextFreeBlock = mEnd;

    // Only one block exists - and it covers the entire usable heap space
    mMinimumEverFreeBytesRemaining = pxFirstFreeBlock->mBlockSize;
    mFreeBytesRemaining = pxFirstFreeBlock->mBlockSize;
    }
  //}}}

  //{{{
  void* alloc (size_t size) {

    void* allocAddress = NULL;
    size_t largestBlock = 0;

    vTaskSuspendAll();
      {
      // The wanted size is increased so it can contain a tLink_t structure in addition to the requested amount of bytes
      size += kHeapStructSize;

      // Ensure that blocks are always aligned to the required number of bytes
      if ((size & portBYTE_ALIGNMENT_MASK) != 0x00)
        // Byte alignment required. */
        size += (portBYTE_ALIGNMENT - (size & portBYTE_ALIGNMENT_MASK));
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
        allocAddress = (void*)(((uint8_t*)prevBlock->mNextFreeBlock) + kHeapStructSize);

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
        if (mFreeBytesRemaining < mMinimumEverFreeBytesRemaining )
          mMinimumEverFreeBytesRemaining = mFreeBytesRemaining;

        // The block is being returned - it is allocated and owned by the application and has no "next" block. */
        block->mBlockSize |= kBlockAllocatedBit;
        block->mNextFreeBlock = NULL;
        }
      }
    xTaskResumeAll();

    if (allocAddress)
      printf ("sdramAlloc %p size:%d free:%d minFree:%d\n",
              allocAddress, size, mFreeBytesRemaining,mMinimumEverFreeBytesRemaining);
    else {
      printf ("***sdramAlloc failed size:%d free:%d minFree:%d largest:%d\n",
              size, mFreeBytesRemaining,mMinimumEverFreeBytesRemaining, largestBlock);

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
  void* allocInt (size_t size) {

    void* allocAddress = NULL;
    size_t largestBlock = 0;

      {
      // The wanted size is increased so it can contain a tLink_t structure in addition to the requested amount of bytes
      size += kHeapStructSize;

      // Ensure that blocks are always aligned to the required number of bytes
      if ((size & portBYTE_ALIGNMENT_MASK) != 0x00)
        // Byte alignment required. */
        size += (portBYTE_ALIGNMENT - (size & portBYTE_ALIGNMENT_MASK));
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
        allocAddress = (void*)(((uint8_t*)prevBlock->mNextFreeBlock) + kHeapStructSize);

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
        if (mFreeBytesRemaining < mMinimumEverFreeBytesRemaining )
          mMinimumEverFreeBytesRemaining = mFreeBytesRemaining;

        // The block is being returned - it is allocated and owned by the application and has no "next" block. */
        block->mBlockSize |= kBlockAllocatedBit;
        block->mNextFreeBlock = NULL;
        }
      }

    return allocAddress;
    }
  //}}}
  //{{{
  void free (void* p) {

    printf ("sdRamFree %p\n", p);

    if (p) {
      // memory being freed will have an tLink_t structure immediately before it.
      uint8_t* puc = (uint8_t*)p - kHeapStructSize;
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

  size_t getFreeHeapSize() { return mFreeBytesRemaining; }
  size_t getMinEverHeapSize() { return mMinimumEverFreeBytesRemaining; }

private:
  //{{{  struct tLink_t
  typedef struct A_BLOCK_LINK {
    struct A_BLOCK_LINK* mNextFreeBlock; // The next free block in the list
    size_t mBlockSize;                   // The size of the free block
    } tLink_t;

  //}}}

  const size_t kHeapStructSize = (sizeof(tLink_t) + ((size_t)(portBYTE_ALIGNMENT-1))) & ~((size_t)portBYTE_ALIGNMENT_MASK);
  const size_t kHeapMinimumBlockSize = kHeapStructSize << 1;
  const size_t kBlockAllocatedBit = 0x80000000;

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
  size_t mFreeBytesRemaining = 0U;
  size_t mMinimumEverFreeBytesRemaining = 0U;
  };
//}}}
cHeap mSdRamHeap;

void sdRamInit (uint32_t start, size_t size) { mSdRamHeap.init (start, size); }
void* sdRamAlloc (size_t size) { return mSdRamHeap.alloc (size); }
void* sdRamAllocInt (size_t size) { return mSdRamHeap.allocInt (size); }
void sdRamFree (void* p) { mSdRamHeap.free (p); }
size_t getSdRamFreeHeapSize() { return mSdRamHeap.getFreeHeapSize(); }
size_t getSdRamGetMinEverHeapSize() { return mSdRamHeap.getMinEverHeapSize(); }
