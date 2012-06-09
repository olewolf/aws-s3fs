/**
 * \file singlelist.c
 * \brief Singly linked list.
 *
 * Copyright (C) 2012 Ole Wolf <wolf@blazingangles.com>
 *
 * This file is part of aws-s3fs.
 * 
 * aws-s3fs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <config.h>
#include <stdlib.h>
#include "singlelist.h"



/**
 * Add a node to the list after the specified node.
 * @param addAfter [in/out] Node after which the node should be inserted.
 * @param toInsert [in/out] Node to be inserted.
 * @return Nothing.
 */
void
AddNodeAfter( struct Node *addAfter, struct Node *toInsert )
{
    toInsert->next = addAfter->next;
    addAfter->next = toInsert;
}


/**
 * Place a node at the head of the list.
 * @param list [in/out] List to which the node should be added as the
 *        first item.
 * @param toInsert [in/out] Node to be inserted.
 * @return Nothing.
 */
void InsertNodeAtHead( struct SingleList *list, struct Node *toInsert )
{
    toInsert->next = list->first;
    list->first    = toInsert;
}


/**
 * Delete a node from a list.
 * @param list [in/out] List from which the node should be deleted.
 * @param toDelete [in] Node to be deleted.
 * @return Nothing.
 */
void
DeleteNode( struct SingleList *list, const struct Node *toDelete )
{
    struct Node *node         = list->first;
    struct Node *previousNode;

    /* Special handling of the first node. */
    if( node == toDelete )
    {
        list->first = toDelete->next;
    }
    else
    {
        /* Linear search for the node by comparing pointers. */
	do
	{
	    previousNode = node;
	    node         = node->next;
	    if( node == toDelete )
	    {
		previousNode->next = toDelete->next;
	    }
	} while( node != NULL );
    }
}
