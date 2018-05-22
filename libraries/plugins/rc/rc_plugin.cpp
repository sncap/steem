#include <steem/plugins/rc/rc_plugin.hpp>
#include <steem/plugins/rc/rc_objects.hpp>

#include <steem/chain/account_object.hpp>
#include <steem/chain/database.hpp>
#include <steem/chain/index.hpp>
#include <steem/chain/operation_notification.hpp>

namespace steem { namespace plugins { namespace rc {

namespace detail {

#define STEEM_RC_REGEN_TIME   (60*60*24*15)

class rc_plugin_impl
{
   public:
      rc_plugin_impl( rc_plugin& _plugin ) :
         _db( appbase::app().get_plugin< steem::plugins::chain::chain_plugin >().db() ),
         _self( _plugin ) {}

      void on_post_apply_transaction( const transaction_notification& note );
      void on_post_apply_block( const block_notification& note );

      database&                     _db;
      rc_plugin&                    _self;
      boost::signals2::connection   _post_apply_transaction_conn;
};

const rc_account_object& get_or_create_rc_account_object( database& db, const account_name_type& account )
{
   const rc_account_object* result = db.find< rc_account_object, by_name >( account );
   if( result != nullptr )
      return *result;

   return db.create< rc_account_object >( rc_account_object& rc_account )
   {
      rc_account.account = account;
      rc_account.rc_usage_last_update = STEEM_GENESIS_TIME;
   };
}

void set_creation_adjustment( database& db, const account_name_type& account )
{
   FC_ASSERT( amount.symbol == STEEM_SYMBOL );
   const rc_account_object& rc_account = get_or_create_rc_account_object( db, account );
}

struct set_creation_adjustment_visitor
{
   typedef void result_type;

   database&                                _db;
   price                                    _vesting_share_price;

   set_creation_adjustment_visitor(
      database& db,
      const price& vsp )
      : _db(db), _vesting_share_price(vsp)
   {}

   void set_creation_adjustment( const account_name_type& account, const asset& fee )
   {
      // NB this "times" is actually division
      asset fee_vests = fee * _vesting_share_price;
      _db.modify( get_or_create_rc_account_object( _db, account ),
         [&]( rc_account_object& rc_account )
         {
            rc_account.max_rc_creation_adjustment = fee_vests.amount.value;
         } );
   }

   void operator()( const account_create_operation& op )const
   {
      set_creation_adjustment( op.new_account_name, op.fee );
   }

   void operator()( const account_create_with_delegation_operation& op )const
   {
      set_creation_adjustment( op.new_account_name, op.fee );
   }
};

void set_creation_adjustments( database& db, const price& vsp, const signed_transaction& tx )
{
   set_creation_adjustment_visitor vtor( db, vsp );
   for( const operation& op : tx.operations )
      vtor.visit( op );
}

account_name_type get_resource_user( const signed_transaction& tx )
{
   flat_set< account_name_type > req_active;
   flat_set< account_name_type > req_owner;
   flat_set< account_name_type > req_posting;
   vector< authority > other;

   for( const operation& op : tx.operations )
   {
      operation_get_required_authorities( op, req_active, req_owner, req_posting, other );
      for( const account_name_type& account : req_active )
         return account;
      for( const account_name_type& account : req_owner )
         return account;
      for( const account_name_type& account : req_posting )
         return account;
   }
   return account_name_type();
}

void use_account_rcs(
   database& db,
   const global_property_object& gpo,
   const account_name_type& account_name,
   int64_t rc )
{
   const account_object& account = db.get_account( account_name );
   const rc_account_object& rc_account = get_or_create_rc_account( db, account_name );

   int64_t dt = int64_t( gpo.time.sec_since_epoch() ) - int64_t( rc_account.rc_usage_last_update.sec_since_epoch() );
   FC_ASSERT( dt >= 0 );

   int64_t rc_usage = rc_account.rc_usage;

   if( rc_usage > 0 )
   {
      uint128_t regen_u128 = uint64_t(dt);
      regen_u128 *= account.vesting_shares;
      regen_u128 /= STEEM_RC_REGEN_TIME;
      uint64_t regen = regen_u128.to_uint64();
      if( regen > uint64_t( rc_usage ) )
         rc_usage = 0;
      else
         rc_usage -= int64_t( regen );
   }

   int64_t rc_max = account.vesting_shares.amount.value;
   rc_max += rc_account.max_rc_creation_adjustment;
   int64_t rc_available = rc_max - rc_usage;

   bool has_rc = (rc <= rc_available);

   if( db.is_producing() )
   {
      STEEM_ASSERT( has_rc, plugin_exception,
         "Account: ${account} needs ${rc_needed} RC, but has ${rc_available} / ${rc_max} RC. Please wait to transact, or power up STEEM.",
         ("account", account_name)
         ("rc_needed", rc)
         ("rc_available", rc_available)
         ("rc_max", rc_max)
         );
   }

   db.modify( rc_account, [&]( rc_account_object& rca )
   {
      rca.rc_usage = rc_usage + rc;
      rca.rc_usage_last_update = gpo.time;
   } );
}

void rc_plugin_impl::on_post_apply_transaction( const transaction_notification& note )
{
   const global_property_object& gpo = _db.get_dynamic_global_properties();

   if( gpo.total_vesting_shares.amount <= 0 )
      return;

   // How many resources does the transaction use?
   count_resources_result_type count;
   count_resources( note.transaction, count );

   // How many RC does this transaction cost?
   const rc_param_object& params_obj = _db.get< rc_param_object, by_id >( rc_param_object::id_type() );
   const rc_pool_object& pool_obj = _db.get< rc_pool_object, by_id >( rc_pool_object::id_type() );

   int64_t total_cost = 0;

   for( size_t i=0; i<STEEM_NUM_RESOURCE_TYPES; i++ )
   {
      const rc_params& params = params_obj.param_array[i];
      int64_t pool = pool_obj.pool_array[i];

      int64_t cost = compute_rc_cost_of_resources( params.curve_params, pool, count.resource_count[i] );
      total_cost += cost;
   }

   // Update any accounts that were created by this transaction based on fee.
   // TODO: Add issue number to HF constant
   if( (count.new_account_op_count > 0) && _db.has_hardfork( STEEM_HARDFORK_0_20 ) )
      set_creation_adjustments( _db, gpo.get_vesting_share_price(), note.transaction );

   account_name_type resource_user = get_resource_user( note.transaction );
   use_account_rcs( _db, gpo, resource_user, total_cost );
}

void rc_plugin_impl::on_post_apply_block( const block_notification& note )
{
   const global_property_object& gpo = _db.get_dynamic_global_properties();

   if( gpo.total_vesting_shares.amount <= 0 )
      return;

   // How many resources does the transaction use?
   count_resources_result_type count;
   for( const signed_transaction& tx : note.block.transactions )
   {
      count_resources( tx, count );
   }

   const rc_param_object& params_obj = _db.get< rc_param_object, by_id >( rc_param_object::id_type() );

enum rc_time_unit_type
{
   rc_time_unit_seconds,
   rc_time_unit_blocks
};

struct rc_curve_params
{
   uint64_t        coeff_a = 0;
   uint64_t        coeff_b = 0;
   int64_t         coeff_d = 0;
};

struct rc_decay_params
{
   uint32_t        decay_per_time_unit = 0;
   uint8_t         decay_per_time_unit_denom_shift = 0;
};

struct rc_resource_params
{
   int8_t          time_unit = rc_time_unit_seconds;
   uint8_t         resource_unit_base = 10;
   uint8_t         resource_unit_exponent = 1;

   rc_curve_params curve_params;
   rc_decay_params decay_params;
};


   db.modify( _db.get< rc_pool_object, by_id >( rc_pool_object::id_type() ),
      [&]( rc_pool_object& pool_obj )
      {
         for( size_t i=0; i<STEEM_NUM_RESOURCE_TYPES; i++ )
         {
            const rc_params& params = params_obj[i];
            int64_t& pool = pool_obj.pool_array[i];
            pool += params.
         }
      } );
}

} // detail

rc_plugin::rc_plugin() {}
rc_plugin::~rc_plugin() {}

void rc_plugin::set_program_options( options_description& cli, options_description& cfg ){}

void rc_plugin::plugin_initialize( const boost::program_options::variables_map& options )
{
   my = std::make_unique< detail::rc_plugin_impl >( *this );
   try
   {
      ilog( "Initializing resource credit plugin" );
      chain::database& db = appbase::app().get_plugin< steem::plugins::chain::chain_plugin >().db();

      my->_post_apply_block_conn = db.add_post_apply_block_handler( [&]( const operation_notification& note ){ my->on_post_apply_block( note ); }, *this, 0 );
      my->_post_apply_transaction_conn = db.add_post_apply_transaction_handler( [&]( const operation_notification& note ){ my->on_post_apply_transaction( note ); }, *this, 0 );

      add_plugin_index< rc_resource_param_index >(db);
      add_plugin_index< rc_pool_index >(db);
      add_plugin_index< rc_account_index >(db);
   }
   FC_CAPTURE_AND_RETHROW()
}

void rc_plugin::plugin_startup() {}

void rc_plugin::plugin_shutdown()
{
   chain::util::disconnect_signal( my->_pre_apply_operation_conn );
   chain::util::disconnect_signal( my->_post_apply_operation_conn );
}

} } } // steem::plugins::rc
