/*
 * libgametasks.c
 *
 *  Created on: Dec. 19, 2019
 *      Author: takis
 */

#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "libgameds.h"

/*************************************************************************
 *
 *
 * PRIVATE FUNCTIONS
 *
 *
 *************************************************************************/
/*
 * function to initialize game at very beginning
 */
static void
prvInitGame (game_t *this_game)
{
  /* initialize GO code books */
  for (size_t i = 0; i != MAX_GO_CODES; ++i)
    {
      /* GO IDs:
       * 			0x0000 000q: players
       * 			0x0000 00q0: aliens
       * 			0x0000 0q00: poohs
       * 			0x0000 q000: expungers
       * 			0x000q 0000: babies
       * 			0x00q0 0000: kitties
       */

      this_game->aliensID[i].code = 0x00000000 + (i << 4);
      this_game->poohsID[i].code = 0x00000000 + (i << 8);
      this_game->expungersID[i].code = 0x00000000 + (i << 12);
      this_game->babiesID[i].code = 0x00000000 + (i << 16);
      this_game->kittiesID[i].code = 0x00000000 + (i << 20);

      this_game->aliensID[i].available = True;
      this_game->poohsID[i].available = True;
      this_game->expungersID[i].available = True;
      this_game->babiesID[i].available = True;
      this_game->kittiesID[i].available = True;
    }

  this_game->number_of_aliens = 0;
  this_game->number_of_poohs = 0;
  this_game->number_of_expungers = 0;
  this_game->number_of_babies = 0;
  this_game->number_of_kitties = 0;

  return;
}

static bool_t
prvGetGOIDCode (go_ID_t *book, uint32_t *code)
{
  bool_t code_found = False;

  /* find an available code */
  for (size_t i = 0; i != MAX_GO_CODES; ++i)
    {
      if (book[i].available)
	{
	  *code = book[i].code;
	  code_found = True;
	  book[i].available = False;
	  break;
	}
    }
  return code_found;
}

/*
 *
 * function determines if an event happens, based on how likely it is...
 * the likelihood is passed in as a parameter
 *
 */

static bool_t
prvYesHappens (likely_t prob)
{
  /*
   HighlyLikely = 0U,		// < RAND_MAX (probability \approx 1)
   QuiteLikely = 1U, 		// < RAND_MAX / 2, prob. = 50 %
   ModeratelyLikely = 2U, 	// < RAND_MAX / 4, prob. = 25 %
   Maybe = 3U,			// < RAND_MAX / 8, prob. = 12.5 %
   Unlikely = 4U,		// < RAND_MAX / 16, prob. = 6.25 %
   QuiteUnLikely = 5U, 		// < RAND_MAX / 32, prob. = 3.125 %
   YeahRight = 6U,		// < RAND_MAX / 64, prob. = 1.5625 %
   */
  int r = rand (); /* pull my finger */
  register int s = RAND_MAX;

  s = s << prob;
  if (r < s)
    {
      return True;
    }
  else
    {
      return False;
    }
}

/*
 *
 * function provides an integer-friendly l1 metric,
 * namely, the sum of the absolute values of the element-wise
 * differences
 *
 */
uint16_t
uiCompareGODistance (go_coord_t A, go_coord_t B)
{
  return abs (A.X - B.X) + abs (A.Y - B.Y);
}

/*
 *
 * function updates GO interaction list of subject
 *
 */
static void
prvUpdateInteractionList (go_t *pSub, go_t *pObj, uint16_t distance,
			  bool_t collision, bool_t seen)
{
  go_list_t *pW = pSub->interactions;
  go_list_t *ppW;
  uint32_t objID = pObj->ID;

  /* look for obj in sub's interaction list */
  bool_t found = 0;
  while (pW != NULL)
    {
      ppW = pW;
      if (pW->ID == objID)
	{
	  found = 1;
	  break;
	}
      pW = pW->pNext;
    }
  if (!found)
    {
      /* add node to interaction list */
      go_list_t *pNew = (go_list_t *) pvPortMalloc (sizeof(go_list_t));
      pNew->ID = objID;
      pNew->distance = distance;
      pNew->seen = seen;
      pNew->collision = collision;
      pNew->pNext = NULL;
      pNew->pPrev = ppW;
      ppW->pNext = pNew;
    }
  else
    {
      /* object is already on the interaction list, update status:
       * 	- if already in collision, must remain so;
       * 	- if unseen, and not in collision, remove from list.
       */
      if (!pW->collision)
	{
	  if (!seen && !collision)
	    {
	      /* remove the node */
	      ppW->pNext = pW->pNext;
	      (pW->pNext)->pPrev = ppW;
	      pvPortFree ((void *) pW);
	    }
	  else
	    {
	      /* update parameters */
	      pW->seen = seen;
	      pW->collision = collision;
	    }
	}
    }
} /* end of prvUpdateInteractionList() */

/*
 *
 * function computes determines the "relationship" between subject and object
 * GOs
 *
 */
static void
prvComputeProximities (go_t* pSubject, go_t* pObject)
{
  go_t *pW = pObject; /* working pointer */
  go_list_t *pWI = pSubject->interactions;

  uint16_t distance;

  while (pW != NULL)
    {
      /* is pObject in contact or seen? */
      distance = uiCompareGODistance (pSubject->pos, pObject->pos);
      if (distance <= THRESHOLD_COLLISION)
	{
	  /* object and subject are in contact */
	  prvUpdateInteractionList (pSubject, pObject, distance, 1, 1);
	}
      else if (distance <= THRESHOLD_SEEN)
	{
	  /* no contact, but object seen by subject */
	  prvUpdateInteractionList (pSubject, pObject, distance, 0, 1);
	}
      else
	{
	  /* no interaction between subject and object, consider removal */
	  prvUpdateInteractionList (pSubject, pObject, distance, 0, 0);
	}
      pW = pW->pNext;
    }
}

/*
 *
 * function deletes all GO nodes, terminates all GO tasks and
 * releases all GO ID codes
 *
 */
static void
prvDeleteAllTasks (game_t *this_game)
{
  go_t *pW = NULL;
  go_t *pTemp = NULL;

  /* eradicate aliens */
  pW = this_game->aliens;
  while (pW != NULL)
    {
      vTaskDelete (pW->task);
      pTemp = pW->pNext; /* save the pointer to the next GO node */
      vPortFree (pW); /* delete current GO node */
      pW = pTemp; /* move on to the next one */
    }
  /* eradicate poohs */
  pW = this_game->poohs;
  while (pW != NULL)
    {
      vTaskDelete (pW->task);
      pTemp = pW->pNext;
      vPortFree (pW);
      pW = pTemp;
    }
  /* eradicate babies */
  pW = this_game->babies;
  while (pW != NULL)
    {
      vTaskDelete (pW->task);
      pTemp = pW->pNext;
      vPortFree (pW);
      pW = pTemp;
    }
  /* eradicate kitties */
  pW = this_game->kitties;
  while (pW != NULL)
    {
      vTaskDelete (pW->task);
      pTemp = pW->pNext;
      vPortFree (pW);
      pW = pTemp;
    }
  /* eradicate expungers */
  pW = this_game->expungers;
  while (pW != NULL)
    {
      vTaskDelete (pW->task);
      pTemp = pW->pNext;
      vPortFree (pW);
      pW = pTemp;
    }
  /* free all codes from GO code book */
  for (size_t i = 0; i != MAX_GO_CODES; ++i)
    {
      /* GO IDs:
       * 			0x0000 000q: players
       * 			0x0000 00q0: aliens
       * 			0x0000 0q00: poohs
       * 			0x0000 q000: expungers
       * 			0x000q 0000: babies
       * 			0x00q0 0000: kitties
       */
      this_game->aliensID[i].available = True;
      this_game->poohsID[i].available = True;
      this_game->expungersID[i].available = True;
      this_game->babiesID[i].available = True;
      this_game->kittiesID[i].available = True;
    }
}

/*
 * Uses addGONode() to create a GO list element, then applies xTaskCreate()
 * to generate the behaviour for that GO
 */
static go_t*
spawnGONodeandTask (game_t *this_game, go_t *pGOHead, uint8_t GOtype,
		    go_coord_t GOstartcoord, go_id_t *codebook,
		    uint32_t GOIDcode)
{
  /* GOtype values (listed here for reference):
   * 		0 -> player
   * 		1 -> alien
   * 		2 -> poohs
   * 		3 -> expungers
   * 		4 -> babies
   * 		5 -> kitties
   */
  /* initialize working pointer */
  if (!GOtype) // if it's a player we're creating then
    {
      if (pGOHead == NULL) // list is empty, create the first player
	{
	  taskENTER_CRITICAL();
	  go_t *pW = addGONode (this_game, pGOHead, 0, GOstartcoord, GOIDcode);
	  xTaskCreate(vPlayerTask, pW->taskText, 256, (void * ) this_game,
		      &(pW->task), GO_TASK_PRIORITY);
	  taskEXIT_CRITICAL();
	  return pW;
	}
    }
  else // any type of game object (GO) other than a player GO
    {
      taskENTER_CRITICAL();
      if (pGOHead == NULL) // the list is empty
	{
	  go_t *pW = addGONode (this_game, pGOHead, GOType, GOstartcoord,
				GOIDCode); // working ptr points to new node
	}
      else
	{
	  /* there is already at least one element in the GO list,
	   * so find the next available spot... */
	  go_t* pW = pGOHead;
	  while (pW->pNext != NULL)
	    {
	      pW = pW->pNext;
	    }
	  go_t* pNew = addGONode (this_game, pGOHead, GOType, GOstartcoord,
				  GOIDCode);
	  pW->pNext = pNew;
	  pW = pNew; // make working ptr point to new node
	}
      switch (GOType)
	{
	case 1:
	  xTaskCreate(vAliensTask, pW->taskText, 256, (void * ) this_game,
		      &(pW->task), GO_TASK_PRIORITY);
	case 2:
	  xTaskCreate(vPoohsTask, pW->taskText, 256, (void * ) this_game,
		      &(pW->task), GO_TASK_PRIORITY);
	case 3:
	  xTaskCreate(vExpungersTask, pW->taskText, 256, (void * ) this_game,
		      &(pW->task), GO_TASK_PRIORITY);
	case 4:
	  xTaskCreate(vBabiesTask, pW->taskText, 256, (void * ) this_game,
		      &(pW->task), GO_TASK_PRIORITY);
	case 5:
	  xTaskCreate(vKittiesTask, pW->taskText, 256, (void * ) this_game,
		      &(pW->task), GO_TASK_PRIORITY);
	}
      taskEXIT_CRITICAL();
      return pW;
    }
}

/****************************************************************************
 *
 *
 * PUBLIC FUNCTIONS
 *
 *
 ****************************************************************************/

void
vPlayerTask (void *pvParams)
{
  game_t *this_game = (game_t *) pvParams;

  // check for extermination

  // update state

}
/*
 *
 * Impacts Task --- the task behind all the game action!
 *
 */
void
vImpactsTask (void *pvParams)
{
  game_t *this_game = (game_t *) pvParams;
  uint32_t GOIDcode;

  /*
   * to begin with, spawn off an alien and two babies...
   */
  if (prvGetGOIDCode (this_game->aliensID, &GOIDcode))
    {
      go_coord_t alien_start_posn =
	{ XMIDDLE, YMIDDLE };
      go_t *pAlien = spawnGONodeandTask (this_game, this_game->aliens, alien,
					 alien_start_posn, this_game->aliensID,
					 GOIDcode);
      pAlien->numlives = 1;
      pAlien->health = 1024;
    }
  if (prvGetGOIDCode (this_game->babiesID, &GOIDcode))
    {
      go_coord_t baby_start_posn_LEFT =
	{ XLEFT, YBOTTOM };
      go_t *pBaby = spawnGONodeandTask (this_game, this_game->babies, baby,
					baby_start_posn_LEFT,
					this_game->babiesID, GOIDcode);
      pBaby->numlives = 1;
      pBaby->health = 128;
    }
  if (prvGetGOIDCode (this_game->babiesID, &GOIDcode))
    {
      go_coord_t baby_start_posn_MID =
	{ XMIDDLE, YBOTTOM };
      go_t *pBaby = spawnGONodeandTask (this_game, this_game->babies, baby,
					baby_start_posn_MID,
					this_game->babiesID, GOIDcode);
      pBaby->numlives = 1;
      pBaby->health = 128;
    }

  /* main loop of Impacts Task */
  uint8_t levelLambda = 0U;

  while (1)
    {

      /*
       * simple way to set the game level:
       * 	level = log2(2*LEVEL_UP_X) = 1 + log2(LEVEL_UP_X)
       */
      levelLambda = this_game->score / (2 * LEVEL_UP_X);
      this_game->game_level = 1 << levelLambda;

      /*
       * is it time to spawn an alien? number of aliens
       * should be (game_level + 1)
       */
      if (this_game->number_of_aliens < (this_game->game_level + 1))
	{
	  /* a high probability exists of an alien being created */
	  if (prvYesHappens (QuiteLikely))
	    {
	      if (prvGetGOIDCode (aliensID, &GOIDcode)) /* is there room for
	       another alien */
		{
		  /* spawn an alien */
		  go_coord_t alien_start_posn =
		    { XMIDDLE, YMIDDLE };
		  go_t *pAlien = spawnGONodeandTask (this_game,
						     this_game->aliens, alien,
						     alien_start_posn,
						     this_game->aliensID,
						     GOIDcode);
		  pAlien->numlives = 1;
		  pAlien->health = 1024;
		}
	    }
	}

      /*
       * is it time to spawn a kitty? The number of kitties should be
       * (game_level)
       */
      if (this_game->number_of_kitties < (this_game->game_level))
	{
	  /* a some probability exists of a kitty showing up,
	   * in which case the alien's pooh may start dropping */
	  if (prvYesHappens (Maybe))
	    {
	      if (prvGetGOIDCode (kittiesID, &GOIDcode)) // kitties available?
		{
		  /* spawn a kitty */
		  go_coord_t kitties_start_posn =
		    { XRIGHT, YBOTTOM };
		  go_t *pKitty = spawnGONodeandTask (this_game,
						     this_game->kitties, kitty,
						     kitties_start_posn,
						     this_game->kittiesID,
						     GOIDcode);
		  pKitty->numlives = 9;
		  pKitty->health = 8192;
		}
	    }
	}

      /*
       *
       * UPDATE THE INTERACTION LISTS
       *
       */
      /* check alien proximity to kitties and expungers */
      pW = this_game->aliens;
      while (pW != NULL)
	{
	  prvComputeProximities (pW, this_game->expungers);
	  prvComputeProximities (pW, this_game->kitties);
	  pW = pW->pNext;
	}
      /* check player proximity to poohs, kitties or babies */
      pW = this_game->player;
      while (pW != NULL)
	{
	  prvComputeProximities (pW, this_game->poohs);
	  prvComputeProximities (pW, this_game->kitties);
	  prvComputeProximities (pW, this_game->babies);
	  pW = pW->pNext;
	}
      /* check babies for proximity to poohs */
      pW = this_game->babies;
      while (pW != NULL)
	{
	  prvComputeProximities (pW, this_game->poohs);
	  pW = pW->pNext;
	}
    }
}

/*
 *
 * High-level supervisory task for each game (one game for each human player)
 *
 */
void
vRunGameTask (void *pvParams)
{
  /* variables */
  size_t player = *((size_t *) pvParams); // the human player
  go_t *pW = NULL;
  xTaskHandle pvImpactsTaskHandle;

  /* this entire game is stored in the following local variable: */
  game_t *this_game = (game_t *) pvPortMalloc (sizeof(game_t));

  /* set some of this game's parameters, just to get us started */
  this_game->score = 0;
  for (size_t i = 0; i != 3; ++i)
    this_game->playerID[i] = 'A';
  this_game->playerID[3] = '\0';
  this_game->level = 1;
  /* initialize GO lists */
  this_game->aliens = NULL;
  this_game->poohs = NULL;
  this_game->expungers = NULL;
  this_game->babies = NULL;
  this_game->kitties = NULL;
  this_game->player = NULL;
  /* initialize code books and record-keeping variables */
  prvInitGame (this_game);

  /* give birth to the player GO for this game (recall that one game is
   * allocated per player) */
  go_coord_t player_start_posn =
    { XMIDDLE, YBOTTOM };
  this_game->player = spawnGONodeandTask (this_game, this_game->player, 0,
					  player_start_posn, NULL, 0x00000001);

  while (1)
    {
      xSemaphoreTake (xGameMutex, portMAX_DELAY);
      go_t *pWplayer = this_game->player; // point to this game's player
      if (pWplayer->active)
	{
	  prvResetBoard ();
	  prvUARTSend ("D: Player %zu", player); // prompt the player
	  vTaskDelay (5 * configTICK_RATE_HZ); // wait 5 seconds

	  /*
	   * create ImpactsTask (with lower priority than RunGameTask())
	   * to run the show
	   */
	  xTaskCreate(vImpactsTask, "Impact-checking Task", 1024,
		      (void * ) &this_game, &pvImpactsTaskHandle,
		      IMPACTS_TASK_PRIORITY);

	  /* keep running until player loses life */
	  while ((pWplayer->alive))
	    {
	      vTaskDelay (DELAY_RUN_GAME); // relinquish control of game
	    }
	  /* player died, update status */
	  if ((--(pWplayer->numlives)) == 0)
	    {
	      pWplayer->gameover = True;
	      vTaskDelete (pvImpactsTaskHandle);
	      prvDeleteAllTasks (this_game);
	      /* inform player */
	      prvUARTSend ("C:clc\n");
	      prvSayGoodbye (pWplayer->ID); /* say goodbye to player,
	       get their initials */
	      vTaskDelay (configTICK_RATE_HZ * 5); /* wait 5 seconds */
	    }
	}
      xSemaphoreGive (xGameMutex);
    } /* end while (1) */
}