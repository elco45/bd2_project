/*
 * Copyright (C) 2008 by The Regents of the University of California
 * Redistribution of this file is permitted under the terms of the GNU
 * Public License (GPL).
 *
 * @author Junghoo "John" Cho <cho AT cs.ucla.edu>
 * @date 3/24/2008
 */
 
#include "BTreeIndex.h"
#include "BTreeNode.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <fstream>
using namespace std;

/*
 * BTreeIndex constructor
 */
BTreeIndex::BTreeIndex()
{
    rootPid = -1;
}

/*
 * Open the index file in read or write mode.
 * Under 'w' mode, the index file should be created if it does not exist.
 * @param indexname[IN] the name of the index file
 * @param mode[IN] 'r' for read, 'w' for write
 * @return error code. 0 if no error
 */
RC BTreeIndex::open(const string& indexname, char mode)
{
  if(pf.open(indexname, mode))
    return 1;

  char info[PageFile::PAGE_SIZE];
  // Settiar el id de la pagina raiz y la altura del arbol
  if (pf.endPid() == 0)
  {
    rootPid = -1;
    treeHeight = 0;
    if (pf.write(0, info))
    {
       return 2;
    }
  }
  else
  {
    if (pf.read(0, info))
    {
      return 2;
    }
    rootPid = *((PageId *)info);
    treeHeight = *((int *)(info+sizeof(PageId)));
  }

  return 0;
}

/*
 * Close the index file.
 * @return error code. 0 if no error
 */
RC BTreeIndex::close()
{
    char info[PageFile::PAGE_SIZE];
    *((PageId *)info) = rootPid;
    *((int *)(info+sizeof(PageId))) = treeHeight;
    pf.write(0,info);

    return pf.close();
}

RC BTreeIndex::insert_helper(int key, const RecordId& rid, PageId pid, int height, int& ofKey, PageId& ofPid)
{
  ofKey = -1;

  // Caso base, cuando esta en nodo hoja
  if (height == treeHeight)
  {
    BTLeafNode ln;
    ln.read(pid, pf);
    if (ln.insert(key, rid))
    {
      // Overflow, se crea un nuevo nodo hoja y se hace split
      BTLeafNode newNode;
      if (ln.insertAndSplit(key, rid, newNode, ofKey))
        return 1;

      // Settea el puntero del nuevo nodo
      ofPid = pf.endPid();
      newNode.setNextNodePtr(ln.getNextNodePtr());
      ln.setNextNodePtr(ofPid);

      if (newNode.write(ofPid, pf))
        return 1;
    }
    if (ln.write(pid, pf))
      return 1;
  }
  // NonLeaf node
  else
  {
    BTNonLeafNode nln;
    int eid;
    PageId child;

    nln.read(pid, pf);
    nln.locate(key, eid);
    nln.readEntry(eid, child);
    insert_helper(key, rid, child, height+1, ofKey, ofPid);
    if (ofKey > 0)
    {
      // Overflow en nodo hijo, se inserta una nueva tupla en el nodo actual
      if (nln.insert(ofKey, ofPid))
      {
        // Divide los hermanos del nodo
        int midKey;
        BTNonLeafNode sibling;

        if (nln.insertAndSplit(ofKey, ofPid, sibling, midKey))
          return 1;
        ofKey = midKey;
        ofPid = pf.endPid();
        if (sibling.write(ofPid, pf))
          return 1;
      }
      else
      {
        ofKey = -1;
      }
      nln.write(pid, pf);
    }
  }
  return 0;
}



/*
 * Insert (key, RecordId) pair to the index.
 * @param key[IN] the key for the value inserted into the index
 * @param rid[IN] the RecordId for the record being inserted into the index
 * @return error code. 0 if no error
 */
RC BTreeIndex::insert(int key, const RecordId& rid)
{
  int ofKey;
  PageId ofPid;

  //Para la primera vez, crear nodo raiz
  if (treeHeight == 0)
  {
    BTLeafNode ln;
    ln.insert(key, rid);
    rootPid = pf.endPid();
    treeHeight = 1;
    ln.write(rootPid, pf);
    return 0;
  }

  if (insert_helper(key, rid, rootPid, 1, ofKey, ofPid))
    return 1;

  // Si hay overflow en el padre, se crea un nuevo nodo raiz
  if (ofKey > 0)
  {
    BTNonLeafNode newRoot;
    newRoot.initializeRoot(rootPid, ofKey, ofPid);
    rootPid = pf.endPid();
    treeHeight++;
    newRoot.write(rootPid, pf);
  }
  return 0;
}

/**
 * Run the standard B+Tree key search algorithm and identify the
 * leaf node where searchKey may exist. If an index entry with
 * searchKey exists in the leaf node, set IndexCursor to its location
 * (i.e., IndexCursor.pid = PageId of the leaf node, and
 * IndexCursor.eid = the searchKey index entry number.) and return 0.
 * If not, set IndexCursor.pid = PageId of the leaf node and
 * IndexCursor.eid = the index entry immediately after the largest
 * index key that is smaller than searchKey, and return the error
 * code RC_NO_SUCH_RECORD.
 * Using the returned "IndexCursor", you will have to call readForward()
 * to retrieve the actual (key, rid) pair from the index.
 * @param key[IN] the key to find
 * @param cursor[OUT] the cursor pointing to the index entry with
 *                    searchKey or immediately behind the largest key
 *                    smaller than searchKey.
 * @return 0 if searchKey is found. Othewise an error code
 */
RC BTreeIndex::locate(int searchKey, IndexCursor& cursor)
{
  PageId pid = rootPid;
  int i = 0;
  while (i < treeHeight-1)
  {
    int eid;
    BTNonLeafNode nln;

    nln.read(pid, pf);
    nln.locateChildPtr(searchKey, eid);
    nln.readEntry(eid, pid);
    i++;
  }

  BTLeafNode ln;
  ln.read(pid, pf);
 
  cursor.pid = pid;
  ln.locate(searchKey, cursor.eid);

  return 0;
}

/*
 * Read the (key, rid) pair at the location specified by the index cursor,
 * and move foward the cursor to the next entry.
 * @param cursor[IN/OUT] the cursor pointing to an leaf-node index entry in the b+tree
 * @param key[OUT] the key stored at the index cursor location.
 * @param rid[OUT] the RecordId stored at the index cursor location.
 * @return error code. 0 if no error
 */
RC BTreeIndex::readForward(IndexCursor& cursor, int& key, RecordId& rid)
{

  BTLeafNode ln;
  ln.read(cursor.pid, pf);
  ln.readEntry(cursor.eid, key, rid);

  //Verifica si hay paginas validas
  if (cursor.pid <= 0 || cursor.pid >= pf.endPid())
  {
    return 1;
  }

  // Incrementa el cursor
  cursor.eid++;
  if (cursor.eid >= ln.getKeyCount())
  {
    cursor.pid = ln.getNextNodePtr();
    cursor.eid = 0;
  }

  return 0;
}
