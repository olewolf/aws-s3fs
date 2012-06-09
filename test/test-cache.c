/**
 * \file test_list.c
 * \brief .
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
#include <stdio.h>
#include "testfunctions.h"
#include "singlelist.h"



void test_FillList( const char * );
void test_DeleteNode( const char * );



struct SingleList list =
{
    .first = NULL
};


int data1 = 1;
int data2 = 2;
int data3 = 3;
int data4 = 4;

struct Node node1 = { NULL, &data1 };
struct Node node2 = { NULL, &data2 };
struct Node node3 = { NULL, &data3 };
struct Node node4 = { NULL, &data4 };



const struct dispatchTable dispatchTable[ ] =
{
    { "FillList", &test_FillList },
    { "DeleteNode", &test_DeleteNode },
    { NULL, NULL }
};


static void PrintList( struct SingleList *list )
{
    int         *data;
    struct Node *node = list->first;
    while( node != NULL )
    {
        data = node->data;
        printf( "%d ", *data );
        node = node->next;
    }
    printf( "\n" );
}



void test_FillList( const char *parms )
{
    /* Add item. */
    InsertNodeAtHead( &list, &node1 );
    AddNodeAfter( &node1, &node2 );
    InsertNodeAtHead( &list, &node3 );
    AddNodeAfter( &node1, &node4 );
    PrintList( &list );
}


void test_DeleteNode( const char *parms )
{
    test_FillList( NULL );
    /* Delete first item. */
    DeleteNode( &list, &node3 );
    PrintList( &list );
    /* Delete second item. */
    DeleteNode( &list, &node4 );
    PrintList( &list );
    /* Delete last item. */
    DeleteNode( &list, &node2 );
    PrintList( &list );
}
