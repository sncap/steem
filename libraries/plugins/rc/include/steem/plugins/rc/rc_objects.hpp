#pragma once
#include <steem/plugins/rc/rc_utility.hpp>

#include <steem/chain/steem_object_types.hpp>

#include <boost/multi_index/composite_key.hpp>

namespace steem { namespace plugins { namespace rc {

using namespace std;
using namespace steem::chain;

#ifndef STEEM_RC_SPACE_ID
#define STEEM_RC_SPACE_ID 16
#endif

enum rc_object_types
{
   rc_resource_param_object_type   = ( STEEM_RC_SPACE_ID << 8 ),
   rc_pool_object_type             = ( STEEM_RC_SPACE_ID << 8 ) + 1,
   rc_account_object_type          = ( STEEM_RC_SPACE_ID << 8 ) + 2
};

class rc_resource_param_object : public object< rc_resource_param_object_type, rc_resource_param_object >
{
   public:
      template< typename Constructor, typename Allocator >
      rc_resource_param_object( Constructor&& c, allocator< Allocator > a )
      {
         c( *this );
      }

      id_type               id;
      fc::array< rc_resource_params, STEEM_NUM_RESOURCE_TYPES >
                            resource_param_array;
};

class rc_pool_object : public object< rc_pool_object_type, rc_pool_object >
{
   public:
      template< typename Constructor, typename Allocator >
      rc_pool_object( Constructor&& c, allocator< Allocator > a )
      {
         c( *this );
         for( size_t i=0; i<STEEM_NUM_RESOURCE_TYPES; i++ )
            rc_pool[i] = 0;
      }

      id_type               id;
      fc::array< int64_t, STEEM_NUM_RESOURCE_TYPES >
                            pool_array;
};

class rc_account_object : public object< rc_account_object_type, rc_account_object >
{
   public:
      template< typename Constructor, typename Allocator >
      rc_account_object( Constructor&& c, allocator< Allocator > a )
      {
         c( *this );
      }

      id_type               id;
      int64_t               rc_balance = 0;
      fc::time_point_sec    rc_balance_last_update;
      int64_t               max_rc_adjustment = 0;
};

class key_lookup_object : public object< key_lookup_object_type, key_lookup_object >
{
   public:
      template< typename Constructor, typename Allocator >
      key_lookup_object( Constructor&& c, allocator< Allocator > a )
      {
         c( *this );
      }

      id_type           id;

      public_key_type   key;
      account_name_type account;
};

typedef key_lookup_object::id_type key_lookup_id_type;


using namespace boost::multi_index;

struct by_key;

typedef multi_index_container<
   key_lookup_object,
   indexed_by<
      ordered_unique< tag< by_id >, member< key_lookup_object, key_lookup_id_type, &key_lookup_object::id > >,
      ordered_unique< tag< by_key >,
         composite_key< key_lookup_object,
            member< key_lookup_object, public_key_type, &key_lookup_object::key >,
            member< key_lookup_object, account_name_type, &key_lookup_object::account >
         >
      >
   >,
   allocator< key_lookup_object >
> key_lookup_index;

} } } // steem::plugins::rc


FC_REFLECT( steem::plugins::rc::key_lookup_object, (id)(key)(account) )
CHAINBASE_SET_INDEX_TYPE( steem::plugins::rc::key_lookup_object, steem::plugins::rc::key_lookup_index )
