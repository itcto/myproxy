
#include "global_id.h"
#include "dbg_log.h"
#include "env.h"

using namespace STREE_TYPES;
using namespace GLOBAL_ENV;


/*
 * class id_cache
 */
id_cache::id_cache(const char *strCache) : 
  m_cacheDesc(strCache)
{
  char *desc = const_cast<char*>(m_cacheDesc.c_str());

  lock_init();

  if (!m_db.init(desc,0,1)) {
    log_print("id cache %s init ok\n", desc);
  }
}

id_cache::~id_cache(void)
{
  m_db.close();

  lock_release();
}

void id_cache::lock_init(void)
{ 
  pthread_mutex_init(&m_lk,0); 
}

void id_cache::lock_release(void) 
{
  pthread_mutex_destroy(&m_lk);
}

int id_cache::int_fetch_and_add(char *key,int &val)
{
  long long dl = sizeof(val);
  const size_t kl = strlen(key);

  val = 0;

  try_lock();
  /* increment original record */
  if (!m_db.fetch(key,kl,&val,dl)) {
    val ++ ;
  }
  /* overwrite the key-value */
  if (m_db.insert(key,kl,&val,dl)) {
    return -1;
  }
  return 0;
}


/*
 * class global_id_hook
 */
global_id_hook::global_id_hook() : m_cache("/tmp/id.cache"),
  m_gidConf(m_conf.m_gidConf)
{
}

global_id_hook::~global_id_hook(void)
{
}

int 
global_id_hook::get_table_name(stxNode *ep, char* &db, char* &tbl)
{
  /* target table in <table> form */
  if (ep->op_lst.size()<=0) {
    tbl = ep->name ;
  }
  /* target in <db>.<table> form */
  else {
    db = ep->name ;
    tbl= ep->op_lst[0]->name ;
  }
  //log_print("targeting insertion table: %s.%s\n",db,tbl);
  return 0;
}

int 
global_id_hook::get_target_col_in_tree(sql_tree &st, stxNode *pf, char *col)
{
  /* try to get global id column */
  stxNode *ptr = 0;
  int pos = 0;

  /* find global id column in format list */
  ptr = st.find_in_tree(pf,col);
  if (!ptr) {
    //log_print("no global id column '%s' found\n",col);
    return -1;
  }
  if ((pos=st.get_parent_pos(ptr))<0) {
    log_print("fatal: found no position in parent\n");
    return -1;
  }
  return pos;
}

int global_id_hook::eliminate_norm_list(sql_tree &st, stxNode *node)
{
  uint16_t i=0;
  stxNode *pe = 0;

  /* eliminate normal list nodes */
  for (pe=node;pe;pe=pe->op_lst[0]) {
    if (pe->op_lst.size()<=0 || 
       pe->op_lst[0]->type!=mktype(m_list,s_norm))
      break ;
  }
  if (pe!=node) {
    for (i=0;i<pe->op_lst.size();i++) {
      st.attach(node,pe->op_lst[i]);
    }
    pe->op_lst.clear();
    st.detach(node->op_lst[0],0);
  }
  return 0;
}

int 
global_id_hook::add_target_col(sql_tree &st, stxNode *pf, stxNode *pv,char *col, int gid)
{
  stxNode *nd = 0;
  char val[PATH_MAX] = "";

  log_print("adding new global id column...\n");

  /* create new node of corresponding column in format list */
  nd = st.create_node(col,m_endp,s_col);
  st.attach(pf,nd);

  /* create new node in value list */
  sprintf(val,"%d",gid);
  nd = st.create_node(val,m_endp,s_c_int);
  st.attach(pv,nd);

  return 1;
}

int 
global_id_hook::deal_target_col(stxNode *pv,int pos, int gid)
{
  if (pos>=static_cast<int>(pv->op_lst.size())) {
    log_print("invalid global column position %d, "
      "value list size %zu\n",pos,pv->op_lst.size());
    return -1;
  }

  pv = pv->op_lst[pos] ;

  /* FIXME: the corresponding item in value list is a place holder */
  if (pv->type==mktype(m_endp,s_ph)) {
    log_print("place-holder-type globaling column "
      "is NOT supported yet\n");
    return 0;
  }

  log_print("changing global id column...\n");

  /* modify value node with global id */
  sprintf(pv->name,"%d",gid);

  return 1;
}

int global_id_hook::update(sql_tree &st, stxNode *pfmt, stxNode *pval,
  const char *key, const char *col)
{
  int gid = 0;
  int ret = 0, pos = 0;
  char *pkey = const_cast<char*>(key);
  char *pcol = const_cast<char*>(col);

  /* 
   * fetch global id 
   */
  m_cache.int_fetch_and_add(pkey,gid);

  if ((pos=get_target_col_in_tree(st,pfmt,pcol))>=0) {
    /* hooks the target column */
    if ((ret=deal_target_col(pval,pos,gid))<=0) {
      return ret;
    }
  } else {
    /* the target column's not present, add one */
    if ((ret=add_target_col(st,pfmt,pval,pcol,gid))<=0) {
      return ret;
    }
  }

  //st.print_tree(root,0);

  return 1;
}

int global_id_hook::run_hook(sql_tree &st,stxNode *root,void *params) 
{
  stxNode *ptr = 0, *pf = 0, *pv = 0;
  char *tbl = 0, *col = 0;
  GLOBALCOL *colInfo = 0;
  char key[PATH_MAX] = "";
  normal_hook_params *para = static_cast<normal_hook_params*>(params);
  char *db = const_cast<char*>(para->m_db) ;

  if (!(ptr=st.find_in_tree(root,mktype(m_stmt,s_insert)))) {
    //log_print("not an insert statement\n");
    return 0;
  }
  /* FIXME:  */
  if (!(pv=st.find_in_tree(root,mktype(m_list,s_val)))) {
    log_print("insert...select is NOT supported yet\n");
    return 0;
  }
  /* FIXME:  */
  if (!(pf=st.find_in_tree(root,mktype(m_list,s_fmt)))) {
    log_print("statement without format list is NOT supported yet\n");
    return 0;
  }

  /* remove normal list under 'root' */
  eliminate_norm_list(st,root);

  /* remove normal list under 'value list' */
  eliminate_norm_list(st,pv);

  /* get targeting insertion table */
  get_table_name(ptr->op_lst[0],db,tbl);

  /* get global id column config */
  colInfo = m_gidConf.get(db,tbl);
  if (!colInfo) {
    log_print("no global id column  "
      "configured for table %s.%s\n",db,tbl);
    return -1;
  }

  /* try to get global id column from tree */
  col = const_cast<char*>(colInfo->col.c_str());

  /* concatanate the key */
  strcat(key,db);
  strcat(key,tbl);
  strcat(key,col);

  /* simply update the global id column if it is
   *  a normal 'insert' statement */
  if (pv->op_lst[0]->type!=mktype(m_list,s_val_sub)) {
    return update(st,pf,pv,key,col);
  }

  /* TODO: the statement looks like 'insert <tbl> <fmt list> <val list 0> <val list 1> ...'  */
  int ret = 0;

  for (auto spv : pv->op_lst) {
    int rc = update(st,pf,spv,key,col);
    ret |= rc>0?1:0 ;
  }

  //st.print_tree(root,0);

  return ret;
}

HMODULE_IMPL (
  global_id_hook,
  glob_id
);


