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
void* pvPortMalloc (size_t xWantedSize) {
  vTaskSuspendAll();
  void* pvReturn = malloc (xWantedSize);
  xTaskResumeAll();
  return pvReturn;
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

//{{{  defines
#define heapBITS_PER_BYTE ((size_t)8)
#define heapMINIMUM_BLOCK_SIZE ((size_t)(xHeapStructSize << 1))
//}}}
//{{{  struct BlockLink_t
typedef struct A_BLOCK_LINK {
  struct A_BLOCK_LINK *pxNextFreeBlock; /*<< The next free block in the list. */
  size_t xBlockSize;            /*<< The size of the free block. */
  } BlockLink_t;

//}}}
//{{{  statics
static const size_t xHeapStructSize = 
  (sizeof(BlockLink_t) + ((size_t)(portBYTE_ALIGNMENT-1))) & ~((size_t)portBYTE_ALIGNMENT_MASK);

static BlockLink_t xStart;
static BlockLink_t* pxEnd = NULL;
static size_t xFreeBytesRemaining = 0U;
static size_t xMinimumEverFreeBytesRemaining = 0U;
static size_t xBlockAllocatedBit = 0;
//}}}

//{{{
void sdRamInit (uint32_t start, uint32_t size) {

  BlockLink_t* pxFirstFreeBlock;
  uint8_t* pucAlignedHeap;

  size_t xTotalHeapSize = size;

  // Ensure the heap starts on a correctly aligned boundary. */
  size_t uxAddress = (size_t)start;
  if ((uxAddress & portBYTE_ALIGNMENT_MASK) != 0) {
    uxAddress += (portBYTE_ALIGNMENT - 1);
    uxAddress &= ~((size_t)portBYTE_ALIGNMENT_MASK);
    xTotalHeapSize -= uxAddress - (size_t)start;
    }

  pucAlignedHeap = (uint8_t*)uxAddress;

  // xStart is used to hold a pointer to the first item in the list of free blocks.
  // The void cast is used to prevent compiler warnings. */
  xStart.pxNextFreeBlock = (void*) pucAlignedHeap;
  xStart.xBlockSize = (size_t)0;

  // pxEnd is used to mark the end of the list of free blocks and is inserted at the end of the heap space. */
  uxAddress = ((size_t)pucAlignedHeap) + xTotalHeapSize;
  uxAddress -= xHeapStructSize;
  uxAddress &= ~((size_t) portBYTE_ALIGNMENT_MASK );
  pxEnd = (void*) uxAddress;
  pxEnd->xBlockSize = 0;
  pxEnd->pxNextFreeBlock = NULL;

  // To start with there is a single free block that is sized to take up the
  // entire heap space, minus the space taken by pxEnd. */
  pxFirstFreeBlock = (void*) pucAlignedHeap;
  pxFirstFreeBlock->xBlockSize = uxAddress - ( size_t ) pxFirstFreeBlock;
  pxFirstFreeBlock->pxNextFreeBlock = pxEnd;

  // Only one block exists - and it covers the entire usable heap space. */
  xMinimumEverFreeBytesRemaining = pxFirstFreeBlock->xBlockSize;
  xFreeBytesRemaining = pxFirstFreeBlock->xBlockSize;

  // Work out the position of the top bit in a size_t variable. */
  xBlockAllocatedBit = ((size_t)1) << ((sizeof(size_t) * heapBITS_PER_BYTE) - 1);
  }
//}}}
//{{{
static void insertBlockIntoFreeList (BlockLink_t* pxBlockToInsert)
{
BlockLink_t *pxIterator;
uint8_t *puc;

  /* Iterate through the list until a block is found that has a higher address
  than the block being inserted. */
  for( pxIterator = &xStart; pxIterator->pxNextFreeBlock < pxBlockToInsert; pxIterator = pxIterator->pxNextFreeBlock )
  {
    /* Nothing to do here, just iterate to the right position. */
  }

  /* Do the block being inserted, and the block it is being inserted after
  make a contiguous block of memory? */
  puc = ( uint8_t * ) pxIterator;
  if( ( puc + pxIterator->xBlockSize ) == ( uint8_t * ) pxBlockToInsert )
  {
    pxIterator->xBlockSize += pxBlockToInsert->xBlockSize;
    pxBlockToInsert = pxIterator;
  }
  else
  {
    mtCOVERAGE_TEST_MARKER();
  }

  /* Do the block being inserted, and the block it is being inserted before
  make a contiguous block of memory? */
  puc = ( uint8_t * ) pxBlockToInsert;
  if( ( puc + pxBlockToInsert->xBlockSize ) == ( uint8_t * ) pxIterator->pxNextFreeBlock )
  {
    if( pxIterator->pxNextFreeBlock != pxEnd )
    {
      /* Form one big block from the two blocks. */
      pxBlockToInsert->xBlockSize += pxIterator->pxNextFreeBlock->xBlockSize;
      pxBlockToInsert->pxNextFreeBlock = pxIterator->pxNextFreeBlock->pxNextFreeBlock;
    }
    else
    {
      pxBlockToInsert->pxNextFreeBlock = pxEnd;
    }
  }
  else
  {
    pxBlockToInsert->pxNextFreeBlock = pxIterator->pxNextFreeBlock;
  }

  /* If the block being inserted plugged a gab, so was merged with the block
  before and the block after, then it's pxNextFreeBlock pointer will have
  already been set, and should not be set here as that would make it point
  to itself. */
  if( pxIterator != pxBlockToInsert )
  {
    pxIterator->pxNextFreeBlock = pxBlockToInsert;
  }
  else
  {
    mtCOVERAGE_TEST_MARKER();
  }
}
//}}}

//{{{
void* sdRamAlloc (size_t xWantedSize) {

  BlockLink_t* pxBlock;
  BlockLink_t* pxPreviousBlock;
  BlockLink_t* pxNewBlockLink;
  void* pvReturn = NULL;

  vTaskSuspendAll();
    {
    // Check the requested block size is not so large that the top bit is set.
    // The top bit of the block size member of the BlockLink_t structure is used to determine who owns the block
    // - the application or the kernel, so it must be free.
    if ((xWantedSize & xBlockAllocatedBit) == 0) {
      // The wanted size is increased so it can contain a BlockLink_t structure in addition to the requested amount of bytes. */
      if (xWantedSize > 0) {
        xWantedSize += xHeapStructSize;

        // Ensure that blocks are always aligned to the required number of bytes. */
        if ((xWantedSize & portBYTE_ALIGNMENT_MASK ) != 0x00)
          // Byte alignment required. */
          xWantedSize += (portBYTE_ALIGNMENT - (xWantedSize & portBYTE_ALIGNMENT_MASK));
        }

      if ((xWantedSize > 0) && (xWantedSize <= xFreeBytesRemaining)) {
        // Traverse the list from the start (lowest address) block until one of adequate size is found. */
        pxPreviousBlock = &xStart;
        pxBlock = xStart.pxNextFreeBlock;
        while ((pxBlock->xBlockSize < xWantedSize) && (pxBlock->pxNextFreeBlock != NULL)) {
          pxPreviousBlock = pxBlock;
          pxBlock = pxBlock->pxNextFreeBlock;
          }

        // If the end marker was reached then a block of adequate size was not found
        if (pxBlock != pxEnd) {
          // Return the memory space pointed to - jumping over the BlockLink_t structure at its start
          pvReturn = (void*)(((uint8_t*)pxPreviousBlock->pxNextFreeBlock) + xHeapStructSize);

          //This block is being returned for use so must be taken out of the list of free blocks
          pxPreviousBlock->pxNextFreeBlock = pxBlock->pxNextFreeBlock;

          // If the block is larger than required it can be split into two.
          if ((pxBlock->xBlockSize - xWantedSize) > heapMINIMUM_BLOCK_SIZE) {
            // This block is to be split into two
            // Create a new block following the number of bytes requested
            // The void cast is used to prevent byte alignment warnings from the compiler
            pxNewBlockLink = (void*)(((uint8_t*)pxBlock) + xWantedSize);

            // Calculate the sizes of two blocks split from the single block
            pxNewBlockLink->xBlockSize = pxBlock->xBlockSize - xWantedSize;
            pxBlock->xBlockSize = xWantedSize;

            // Insert the new block into the list of free blocks
            insertBlockIntoFreeList( pxNewBlockLink );
            }

          xFreeBytesRemaining -= pxBlock->xBlockSize;
          if (xFreeBytesRemaining < xMinimumEverFreeBytesRemaining )
            xMinimumEverFreeBytesRemaining = xFreeBytesRemaining;

          // The block is being returned - it is allocated and owned by the application and has no "next" block. */
          pxBlock->xBlockSize |= xBlockAllocatedBit;
          pxBlock->pxNextFreeBlock = NULL;
          }
        }
      }
    }
  xTaskResumeAll();

  printf ("sdramAlloc %p %d\n", pvReturn, xWantedSize);
  return pvReturn;
  }
//}}}
//{{{
void sdRamFree (void* p) {

  printf ("sdRamFree %p\n", p);

  uint8_t* puc = (uint8_t*)p;
  BlockLink_t* pxLink;

  if (p != NULL) {
    // The memory being freed will have an BlockLink_t structure immediately before it.
    puc -= xHeapStructSize;

    // casting is to keep the compiler from issuing warnings.
    pxLink = (void*)puc;

    if ((pxLink->xBlockSize & xBlockAllocatedBit ) != 0) {
      if (pxLink->pxNextFreeBlock == NULL) {
        // The block is being returned to the heap - it is no longer allocated.
        pxLink->xBlockSize &= ~xBlockAllocatedBit;

        vTaskSuspendAll();
          {
          // Add this block to the list of free blocks.
          xFreeBytesRemaining += pxLink->xBlockSize;
          insertBlockIntoFreeList (((BlockLink_t*)pxLink));
          }
        xTaskResumeAll();
        }
      }
    }
  }
//}}}
size_t getSdRamFreeHeapSize() { return xFreeBytesRemaining; }
size_t getSdRamGetMinEverHeapSize() { return xMinimumEverFreeBytesRemaining; }
