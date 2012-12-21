#include "openwsn.h"
#include "schedule.h"
#include "openserial.h"
#include "idmanager.h"
#include "openrandom.h"
#include "board_info.h"
#include "processIE.h"
#include "reservation.h"
#include "packetfunctions.h"

//=========================== variables =======================================

typedef struct {
   scheduleEntry_t  scheduleBuf[MAXACTIVESLOTS];
   scheduleEntry_t* currentScheduleEntry;
   uint16_t         frameLength;
   slotOffset_t     debugPrintRow;
} schedule_vars_t;

schedule_vars_t schedule_vars;

typedef struct {
   uint8_t          numActiveSlotsCur;
   uint8_t          numActiveSlotsMax;
} schedule_dbg_t;

schedule_dbg_t schedule_dbg;

Link_t links[MAXACTIVESLOTS];


//=========================== prototypes ======================================

void schedule_resetEntry(scheduleEntry_t* pScheduleEntry);
bool schedule_checkExistSchedule(uint16_t slotOffset);

// check link
bool isOneAvailableLink(Link_t tempLink);
bool isOneRequestedLink(Link_t tempLink);


//=========================== public ==========================================

//=== admin

void schedule_init() {
   uint8_t     i;
   open_addr_t temp_neighbor;

   // reset local variables
   memset(&schedule_vars,0,sizeof(schedule_vars_t));
   memset(&schedule_dbg, 0,sizeof(schedule_dbg_t));
   for (i=0;i<MAXACTIVESLOTS;i++){
      schedule_resetEntry(&schedule_vars.scheduleBuf[i]);
   }

   // set frame length
   schedule_setFrameLength(9);
 

   // slot 0 is advertisement slot
   i = 0;
   memset(&temp_neighbor,0,sizeof(temp_neighbor));
   schedule_addActiveSlot(i,
         CELLTYPE_ADV,
         FALSE,
         0,
         &temp_neighbor);

   if (idmanager_getIsDAGroot() == TRUE) {
   // slot 1 is shared TXRX anycast
   i = 1;
   memset(&temp_neighbor,0,sizeof(temp_neighbor));
   temp_neighbor.type             = ADDR_ANYCAST;
   schedule_addActiveSlot(i,
         CELLTYPE_TXRX,
         TRUE,
         0,
         &temp_neighbor);
   }

   // slot 2 is SERIALRX
   i = 2;
   memset(&temp_neighbor,0,sizeof(temp_neighbor));
   schedule_addActiveSlot(i,
         CELLTYPE_SERIALRX,
         FALSE,
         0,
         &temp_neighbor);

   // slot 3 is MORESERIALRX
   i = 3;
   memset(&temp_neighbor,0,sizeof(temp_neighbor));
   schedule_addActiveSlot(i,
         CELLTYPE_MORESERIALRX,
         FALSE,
         0,
         &temp_neighbor);

   // slot 4 is MORESERIALRX
   i = 4;
   memset(&temp_neighbor,0,sizeof(temp_neighbor));
   schedule_addActiveSlot(i,
         CELLTYPE_MORESERIALRX,
         FALSE,
         0,
         &temp_neighbor);
}

bool debugPrint_schedule() {
   debugScheduleEntry_t temp;
   schedule_vars.debugPrintRow    = (schedule_vars.debugPrintRow+1)%MAXACTIVESLOTS;
   temp.row                       = schedule_vars.debugPrintRow;
   temp.scheduleEntry             = schedule_vars.scheduleBuf[schedule_vars.debugPrintRow];
   openserial_printStatus(STATUS_SCHEDULE,
         (uint8_t*)&temp,
         sizeof(debugScheduleEntry_t));
   return TRUE;
}

//=== from uRES (writing the schedule)

/**
\brief Set frame length.

\param newFrameLength The new frame length.
 */
void schedule_setFrameLength(frameLength_t newFrameLength) {
   INTERRUPT_DECLARATION();
   DISABLE_INTERRUPTS();
   schedule_vars.frameLength = newFrameLength;
   ENABLE_INTERRUPTS();
}

/**
\brief Add a new active slot into the schedule.

\param newFrameLength The new frame length.
 */
void schedule_addActiveSlot(slotOffset_t    slotOffset,
                            cellType_t      type,
                            bool            shared,
                            uint8_t         channelOffset,
                            open_addr_t*    neighbor) {
   scheduleEntry_t* slotContainer;
   scheduleEntry_t* previousSlotWalker;
   scheduleEntry_t* nextSlotWalker;
   INTERRUPT_DECLARATION();
   DISABLE_INTERRUPTS();

   // find an empty schedule entry container
   slotContainer = &schedule_vars.scheduleBuf[0];
   while (slotContainer->type!=CELLTYPE_OFF &&
         slotContainer<=&schedule_vars.scheduleBuf[MAXACTIVESLOTS-1]) {
      slotContainer++;
   }
   if (slotContainer>&schedule_vars.scheduleBuf[MAXACTIVESLOTS-1]) {
      // schedule has overflown
      while(1);
   }
   // fill that schedule entry with parameters passed
   slotContainer->slotOffset                = slotOffset;
   slotContainer->type                      = type;
   slotContainer->shared                    = shared;
   slotContainer->channelOffset             = channelOffset;
   memcpy(&slotContainer->neighbor,neighbor,sizeof(open_addr_t));

   if (schedule_vars.currentScheduleEntry==NULL) {
      // this is the first active slot added

      // the next slot of this slot is this slot
      slotContainer->next                   = slotContainer;

      // current slot points to this slot
      schedule_vars.currentScheduleEntry    = slotContainer;
   } else  {
      // this is NOT the first active slot added

      // find position in schedule
      previousSlotWalker                    = schedule_vars.currentScheduleEntry;
      while (1) {
         nextSlotWalker                     = previousSlotWalker->next;
         if (
               (
                     (previousSlotWalker->slotOffset <  slotContainer->slotOffset) &&
                     (slotContainer->slotOffset <  nextSlotWalker->slotOffset)
               )
               ||
               (
                     (previousSlotWalker->slotOffset <  slotContainer->slotOffset) &&
                     (nextSlotWalker->slotOffset <= previousSlotWalker->slotOffset)
               )
               ||
               (
                     (slotContainer->slotOffset <  nextSlotWalker->slotOffset) &&
                     (nextSlotWalker->slotOffset <= previousSlotWalker->slotOffset)
               )
         ) {
            break;
         }
         previousSlotWalker                 = nextSlotWalker;
      }
      // insert between previousSlotWalker and nextSlotWalker
      previousSlotWalker->next              = slotContainer;
      slotContainer->next                   = nextSlotWalker;
   }

   // maintain debug stats
   schedule_dbg.numActiveSlotsCur++;
   if (schedule_dbg.numActiveSlotsCur>schedule_dbg.numActiveSlotsMax) {
      schedule_dbg.numActiveSlotsMax        = schedule_dbg.numActiveSlotsCur;
   }
   ENABLE_INTERRUPTS();
}

void    schedule_removeActiveSlot(slotOffset_t    slotOffset,
                                  cellType_t      type,
                                  uint8_t         channelOffset,
                                  open_addr_t*    neighbor){
        scheduleEntry_t* slotContainer;

   INTERRUPT_DECLARATION();
   DISABLE_INTERRUPTS();
   
   	// find if SlotOffset has been used
	slotContainer = &schedule_vars.scheduleBuf[0];
	while (slotContainer<=&schedule_vars.scheduleBuf[MAXACTIVESLOTS-1]) {
          if ((packetfunctions_sameAddress(&slotContainer->neighbor, neighbor))&&
             (slotContainer->type ==type) && 
             (slotContainer->slotOffset == slotOffset)&&
             (slotContainer->channelOffset == channelOffset)  
               )
                break;
           else 
		slotContainer++;
	}
        //copy nextSlotContainer to slotContainer, then delete nextSlotContainer
        scheduleEntry_t* nextSlotContainer = slotContainer->next;
        memcpy(slotContainer,slotContainer->next,sizeof(scheduleEntry_t));
        memset(nextSlotContainer, 0, sizeof(scheduleEntry_t));
        
        // maintain debug stats
        schedule_dbg.numActiveSlotsCur--;
   
   ENABLE_INTERRUPTS();
   
}

void    schedule_setMySchedule(uint8_t slotframeID,uint16_t slotframeSize,uint8_t numOfLink,open_addr_t* previousHop){
  //set schedule according links
  open_addr_t temp_neighbor;
  for(uint8_t i = 0;i<numOfLink;i++)
  {
    if(schedule_checkExistSchedule(links[i].slotOffset) == FALSE)
    {
      if(links[i].linktype == CELLTYPE_TXRX)
      {
         memcpy(&temp_neighbor,previousHop,sizeof(open_addr_t));
         schedule_addActiveSlot(links[i].slotOffset,
            CELLTYPE_TXRX,
            FALSE,
            links[i].channelOffset,
            &temp_neighbor);
      }
    }
  }
  memset(links,0,MAXACTIVESLOTS*sizeof(Link_t));
}
//if msg to be send is created by reservation module, 
//send msg at TXRX slot. Or send it at TX slot. The 
//motivation to write this function is that I want 
//to send msg at slot generated by reservation module.
slotOffset_t    schedule_getSlotToSendPacket(OpenQueueEntry_t* msg,open_addr_t*     neighbor){
   scheduleEntry_t* slotContainer = &schedule_vars.scheduleBuf[0];
   while (slotContainer<=&schedule_vars.scheduleBuf[MAXACTIVESLOTS-1]){
     if(msg->creator == COMPONENT_RESERVATION && slotContainer->type == CELLTYPE_TXRX){
        if(idmanager_getIsDAGroot())
          return slotContainer->slotOffset;
        else if(packetfunctions_sameAddress(&slotContainer->neighbor, neighbor))
          return slotContainer->slotOffset;
     }
     if((msg->creator != COMPONENT_RESERVATION) && 
        (slotContainer->type == CELLTYPE_TX) &&
        (packetfunctions_sameAddress(&slotContainer->neighbor, neighbor)))
       return slotContainer->slotOffset;
     
    slotContainer++;
   }
   //return 0 if no slot to send msg
   return 0;
}

//=== from IEEE802154E: reading the schedule and updating statistics

void schedule_syncSlotOffset(slotOffset_t targetSlotOffset) {
   INTERRUPT_DECLARATION();
   DISABLE_INTERRUPTS();
   while (schedule_vars.currentScheduleEntry->slotOffset!=targetSlotOffset) {
      schedule_advanceSlot();
   }
   ENABLE_INTERRUPTS();
}

void schedule_advanceSlot() {
   INTERRUPT_DECLARATION();
   DISABLE_INTERRUPTS();
   // advance to next active slot
   schedule_vars.currentScheduleEntry = schedule_vars.currentScheduleEntry->next;
   ENABLE_INTERRUPTS();
}

slotOffset_t schedule_getNextActiveSlotOffset() {
   slotOffset_t res;   
   INTERRUPT_DECLARATION();
   
   // return next active slot's slotOffset
   DISABLE_INTERRUPTS();
   res = ((scheduleEntry_t*)(schedule_vars.currentScheduleEntry->next))->slotOffset;
   ENABLE_INTERRUPTS();
   
   return res;
}

/**
\brief Get the frame length.

\returns The frame length.
 */
frameLength_t schedule_getFrameLength() {
   frameLength_t res;
   INTERRUPT_DECLARATION();
   
   DISABLE_INTERRUPTS();
   res= schedule_vars.frameLength;
   ENABLE_INTERRUPTS();
   
   return res;
   
}

/**
\brief Get the type of the current schedule entry.

\returns The type of the current schedule entry.
 */
 cellType_t schedule_getType() {
    cellType_t res;
    INTERRUPT_DECLARATION();
    DISABLE_INTERRUPTS();
    res= schedule_vars.currentScheduleEntry->type;
    ENABLE_INTERRUPTS();
         return res;
}

/**
\brief Get the neighbor associated wit the current schedule entry.

\returns The neighbor associated wit the current schedule entry.
 */
 void schedule_getNeighbor(open_addr_t* addrToWrite) {
   INTERRUPT_DECLARATION();
   DISABLE_INTERRUPTS();
   memcpy(addrToWrite,&(schedule_vars.currentScheduleEntry->neighbor),sizeof(open_addr_t));
   ENABLE_INTERRUPTS();
}

/**
\brief Get the channel offset of the current schedule entry.

\returns The channel offset of the current schedule entry.
 */
channelOffset_t schedule_getChannelOffset() {
   channelOffset_t res;
   INTERRUPT_DECLARATION();
   DISABLE_INTERRUPTS();
   res= schedule_vars.currentScheduleEntry->channelOffset;
   ENABLE_INTERRUPTS();
   return res;
}

/**
\brief Check whether I can send on this slot.

This function is called at the beginning of every TX slot. If the slot is not a
shared slot, it always return TRUE. If the slot is a shared slot, it decrements
the backoff counter and returns TRUE only if it hits 0.

\returns TRUE if it is OK to send on this slot, FALSE otherwise.
 */
bool schedule_getOkToSend() {
   INTERRUPT_DECLARATION();
   DISABLE_INTERRUPTS();
   // decrement backoff of that slot
   if (schedule_vars.currentScheduleEntry->backoff>0) {
      schedule_vars.currentScheduleEntry->backoff--;
   }
   // check whether backoff has hit 0
   if (
         schedule_vars.currentScheduleEntry->shared==FALSE ||
         (
               schedule_vars.currentScheduleEntry->shared==TRUE &&
               schedule_vars.currentScheduleEntry->backoff==0
         )
   ) {
      ENABLE_INTERRUPTS();
      return TRUE;
   } else {
      ENABLE_INTERRUPTS();
      return FALSE;
   }
}

/**
\brief Indicate the reception of a packet.
 */
void schedule_indicateRx(asn_t* asnTimestamp) {
   INTERRUPT_DECLARATION();
   DISABLE_INTERRUPTS();
   // increment usage statistics
   schedule_vars.currentScheduleEntry->numRx++;

   // update last used timestamp
   memcpy(&(schedule_vars.currentScheduleEntry->lastUsedAsn), asnTimestamp, sizeof(asn_t));
   ENABLE_INTERRUPTS();
}

/**
\brief Indicate the transmission of a packet.
 */
 void schedule_indicateTx(asn_t*   asnTimestamp,
   bool     succesfullTx) {
   
   INTERRUPT_DECLARATION();
   DISABLE_INTERRUPTS();
   // increment usage statistics
   if (schedule_vars.currentScheduleEntry->numTx==0xFF) {
      schedule_vars.currentScheduleEntry->numTx/=2;
      schedule_vars.currentScheduleEntry->numTxACK/=2;
   }
   schedule_vars.currentScheduleEntry->numTx++;
   if (succesfullTx==TRUE) {
      schedule_vars.currentScheduleEntry->numTxACK++;
   }

   // update last used timestamp
   memcpy(&schedule_vars.currentScheduleEntry->lastUsedAsn, asnTimestamp, sizeof(asn_t));

   // update this slot's backoff parameters
   if (succesfullTx==TRUE) {
      // reset backoffExponent
      schedule_vars.currentScheduleEntry->backoffExponent   = MINBE-1;
      // reset backoff
      schedule_vars.currentScheduleEntry->backoff           = 0;
   } else {
      // increase the backoffExponent
      if (schedule_vars.currentScheduleEntry->backoffExponent<MAXBE) {
         schedule_vars.currentScheduleEntry->backoffExponent++;
      }
      // set the backoff to a random value in [0..2^BE]
      schedule_vars.currentScheduleEntry->backoff =
            openrandom_get16b()%(1<<schedule_vars.currentScheduleEntry->backoffExponent);
   }
   ENABLE_INTERRUPTS();
}

//TODO, check that the number of bytes is not bigger than maxbytes. If so, retun error.
void schedule_getNetDebugInfo(netDebugScheduleEntry_t *schlist, uint8_t maxbytes){
  
  uint8_t i;
  for (i=0;i<MAXACTIVESLOTS;i++){
   schlist[i].last_addr_byte=schedule_vars.scheduleBuf[i].neighbor.addr_64b[7];
   schlist[i].slotOffset=(uint8_t)schedule_vars.scheduleBuf[i].slotOffset&0xFF;
   schlist[i].channelOffset=schedule_vars.scheduleBuf[i].channelOffset;
  }    

}

 // from processIE
uint8_t schedule_getNumSlotframe(){
  return 1;
}

frameLength_t   schedule_getSlotframeSize(uint8_t numOfSlotframe){
   frameLength_t res;
   INTERRUPT_DECLARATION();
   
   DISABLE_INTERRUPTS();
   res= schedule_vars.frameLength;
   ENABLE_INTERRUPTS();
   
   return res;
}

uint8_t schedule_getLinksNumber(uint8_t slotframeID){
    uint8_t i;
    for (i=0;i<MAXACTIVESLOTS;i++){
      if(links[i].linktype == CELLTYPE_OFF)
        break;
   }
   return i;

}

Link_t* schedule_getLinksList(uint8_t slotframeID){
    //return link list
    return links;
}

void schedule_generateLinkList(uint8_t slotframeID){
    uint8_t j = 0;
    // for test
     for (uint8_t i=0;i<MAXACTIVESLOTS;i++){
       if(schedule_vars.scheduleBuf[i].type == CELLTYPE_TXRX)
       {
        links[j].channelOffset = schedule_vars.scheduleBuf[i].channelOffset;
        links[j].slotOffset = schedule_vars.scheduleBuf[i].slotOffset;
        links[j].linktype = schedule_vars.scheduleBuf[i].type;
        j++;
       }
   }
}

scheduleEntry_t* schedule_getScheduleEntry(uint16_t slotOffset){
    scheduleEntry_t* tempScheduleEntry = schedule_vars.currentScheduleEntry;
  
  do
  {
    if(slotOffset == tempScheduleEntry->slotOffset)
      return tempScheduleEntry;
    
    tempScheduleEntry = tempScheduleEntry->next;
    
  }while(tempScheduleEntry != schedule_vars.currentScheduleEntry);
  return NULL;
}

bool isOneAvailableLink(Link_t tempLink){
  scheduleEntry_t* tempScheduleEntry = schedule_vars.currentScheduleEntry;
  
  do
  {
    if(tempLink.slotOffset == tempScheduleEntry->slotOffset)
      return FALSE;
    
    tempScheduleEntry = tempScheduleEntry->next;
    
  }while(tempScheduleEntry != schedule_vars.currentScheduleEntry);
  return TRUE;
}

bool isOneRequestedLink(Link_t tempLink){
  for(uint8_t i=0;i<MAXACTIVESLOTS;i++)
  {
    if(tempLink.slotOffset == links[i].slotOffset)
      return FALSE;
  }
  return TRUE;
}

void    schedule_uResGenerateCandidataLinkList(uint8_t slotframeID){
    uint8_t j = 0;
    Link_t tempLink;
    // for test
     for (uint8_t i=0;i<MAXACTIVESLOTS;i++)
     {
        tempLink.channelOffset = 0;
        tempLink.slotOffset = i;
        tempLink.linktype = CELLTYPE_TX;
        if(isOneAvailableLink(tempLink) && isOneRequestedLink(tempLink))
        {
          links[j].channelOffset        = tempLink.channelOffset;
          links[j].slotOffset           = tempLink.slotOffset;
          links[j].linktype             = tempLink.linktype;
          j++;
        }
      }
}

void    schedule_uResGenerateRemoveLinkList(uint8_t slotframeID,Link_t tempLink){
    // this function should  be called by upper layers to generate links to be remove
    // if you want to use reservation to remove links, you should add them to array links[MAXACTIVESLOTS]
        links[0] = tempLink;
}

// from reservation
void    schedule_addLinksToSchedule(uint8_t slotframeID,open_addr_t* previousHop,uint8_t numOfLinks,uint8_t state){
    //set schedule according links
  open_addr_t temp_neighbor;
  for(uint8_t i = 0;i<numOfLinks;i++)
  {
    if(schedule_checkExistSchedule(links[i].slotOffset) == FALSE)
    {
      if(links[i].linktype == CELLTYPE_TX)
      {
        switch(state) {
          case S_RESLINKREQUEST_RECEIVE:
            memcpy(&temp_neighbor,previousHop,sizeof(open_addr_t));
            //add a RX link
            schedule_addActiveSlot(links[i].slotOffset,
              CELLTYPE_RX,
              FALSE,
              links[i].channelOffset,
              &temp_neighbor);
            break;
          case S_RESLINKRESPONSE_RECEIVE:
            memcpy(&temp_neighbor,previousHop,sizeof(open_addr_t));
            //add a TX link
            schedule_addActiveSlot(links[i].slotOffset,
              CELLTYPE_TX,
              FALSE,
              links[i].channelOffset,
              &temp_neighbor);
            memset(&(links[0]),0,MAXACTIVESLOTS*sizeof(Link_t));
            break;
          default:
          //log error
            break;
        }

      }
    }
  }
}

void    schedule_allocateLinks(uint8_t slotframeID,uint8_t numOfLink,uint8_t bandwidth){
  
  uint8_t j = 0;
  
  for(uint8_t i=0;i<numOfLink;i++)
  {
    if(isOneAvailableLink(links[i]))
    {
      if(i>j)
        memcpy(&(links[j]),&(links[i]),sizeof(Link_t));
      j++;
      if(j == bandwidth)
      {
        memset(&(links[j]),0,(MAXACTIVESLOTS-j)*sizeof(Link_t));
        return;
      }
    }
  }
  memset(&(links[0]),0,MAXACTIVESLOTS*sizeof(Link_t));
}

void    schedule_removeLinksFromSchedule(uint8_t slotframeID,uint16_t slotframeSize,uint8_t numOfLink,open_addr_t* previousHop,uint8_t state){
  //set schedule according links
  open_addr_t temp_neighbor;
  scheduleEntry_t* tempScheduleEntry;
  for(uint8_t i = 0;i<numOfLink;i++)
  {
    if(schedule_checkExistSchedule(links[i].slotOffset))
    {
      tempScheduleEntry = schedule_getScheduleEntry(links[i].slotOffset);
      
      if(tempScheduleEntry == NULL)
      {
        //log error
        return;
      }
      //get reference neighbor of Slot
      memcpy(&(temp_neighbor),&(tempScheduleEntry->neighbor),sizeof(open_addr_t));
      
      if(links[i].linktype == CELLTYPE_RX && packetfunctions_sameAddress(&(temp_neighbor),previousHop))
      {
        switch (state){
          case S_REMOVELINKREQUEST_SEND:    
              // remove CELLTYPE_TX link from shedule
            schedule_removeActiveSlot(links[i].slotOffset,
              CELLTYPE_TX,
              links[i].channelOffset,
              &(temp_neighbor));
            break;
          case S_REMOVELINKREQUEST_RECEIVE:
            //remove CELLTYPE_RX link from shedule
            schedule_removeActiveSlot(links[i].slotOffset,
              CELLTYPE_RX,
              links[i].channelOffset,
              &(temp_neighbor));
              memset(links,0,MAXACTIVESLOTS*sizeof(Link_t));
            break;
        default:
          //log error
        }

      }
    }
  }
}

//=========================== private =========================================

void schedule_resetEntry(scheduleEntry_t* pScheduleEntry) {
   pScheduleEntry->type                     = CELLTYPE_OFF;
   pScheduleEntry->shared                   = FALSE;
   pScheduleEntry->backoffExponent          = MINBE-1;
   pScheduleEntry->backoff                  = 0;
   pScheduleEntry->channelOffset            = 0;
   pScheduleEntry->neighbor.type            = ADDR_NONE;
   pScheduleEntry->neighbor.addr_64b[0]     = 0x14;
   pScheduleEntry->neighbor.addr_64b[1]     = 0x15;
   pScheduleEntry->neighbor.addr_64b[2]     = 0x92;
   pScheduleEntry->neighbor.addr_64b[3]     = 0x09;
   pScheduleEntry->neighbor.addr_64b[4]     = 0x02;
   pScheduleEntry->neighbor.addr_64b[5]     = 0x2c;
   pScheduleEntry->neighbor.addr_64b[6]     = 0x00;
   pScheduleEntry->numRx                    = 0;
   pScheduleEntry->numTx                    = 0;
   pScheduleEntry->numTxACK                 = 0;
   pScheduleEntry->lastUsedAsn.bytes0and1   = 0;
   pScheduleEntry->lastUsedAsn.bytes2and3   = 0;
   pScheduleEntry->lastUsedAsn.byte4        = 0;
}

bool schedule_checkExistSchedule(uint16_t slotOffset){
   for (uint8_t i=0;i<MAXACTIVESLOTS;i++){
      if(schedule_vars.scheduleBuf[i].slotOffset == slotOffset)
        return TRUE;
   }
  return FALSE;
}