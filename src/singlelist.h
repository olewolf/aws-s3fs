/**
 * \file singlelist.h
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


struct Node
{
    struct Node *next;
    void        *data;
};


struct SingleList
{
    struct Node *first;
};


void AddNodeAfter( struct Node *addAfter, struct Node *toInsert );
void InsertNodeAtHead( struct SingleList *list, struct Node *toInsert );
void DeleteNode( struct SingleList *list, const struct Node *toDelete );
