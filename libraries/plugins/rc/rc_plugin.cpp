#include <steem/plugins/rc/rc_plugin.hpp>
#include <steem/plugins/rc/rc_objects.hpp>

#include <steem/chain/account_object.hpp>
#include <steem/chain/database.hpp>
#include <steem/chain/index.hpp>
#include <steem/chain/operation_notification.hpp>

namespace steem { namespace plugins { namespace rc {

namespace detail {

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

void rc_plugin_impl::create_rc_accounts( const vector< account_creation >& account_creations )
{
   size_t num_accounts = _db.count< account_object_type >();
   size_t num_rc_accounts = _db.count< rc_account_object >();

   FC_ASSERT( num_rc_accounts <= num_accounts );
   size_t delta_accounts = num_rc_accounts - num_accounts;

   if( delta_accounts != account_creations.size() )
      create_rc_accounts_stateless( account_creations );

   if( delta_accounts > account_creations.size() )
   {
      // Some accounts were created that weren't listed in account_creations.
      // This includes genesis accounts and PoW accounts.
      for( int64_t i=int64_t( num_rc_accounts ); i<int64_t( num_accounts ); i++ )
      {
         // 
      }
   }
}

void rc_plugin_impl::on_post_apply_transaction( const transaction_notification& note )
{
   const global_property_object& gpo = _db.get_dynamic_global_properties();

   if( gpo.total_vesting_shares.amount <= 0 )
      return;

   // How many resources does the transaction use?
   count_resources_result_type count;
   count_resources( note.transaction, count );
   create_rc_accounts( count.account_creations );

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


   while( true )
   {
      
   }


   const rc_account_object& rc_account = _db.find< rc_account_object, 
         auto band = _db.find< account_bandwidth_object, by_account_bandwidth_type >( boost::make_tuple( a.name, type ) );

   bool has_rc = (total_cost > 

   if( _db.is_producing() )
   {
      STEEM_ASSERT( has_bandwidth,  plugin_exception,
            "Account: ${account} bandwidth limit exceeded. Please wait to transact or power up STEEM.",
            ("account", a.name)
            ("account_vshares", account_vshares)
            ("account_average_bandwidth", account_average_bandwidth)
            ("max_virtual_bandwidth", max_virtual_bandwidth)
            ("total_vesting_shares", total_vshares) );
   }
}

void rc_plugin_impl::on_post_apply_block( const block_notification& note )
{
   
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
      ilog( "Initializing rc plugin" );
      chain::database& db = appbase::app().get_plugin< steem::plugins::chain::chain_plugin >().db();

      my->_pre_apply_operation_conn = db.add_pre_apply_operation_handler( [&]( const operation_notification& note ){ my->on_pre_apply_operation( note ); }, *this, 0 );
      my->_post_apply_operation_conn = db.add_post_apply_operation_handler( [&]( const operation_notification& note ){ my->on_post_apply_operation( note ); }, *this, 0 );

      add_plugin_index< key_lookup_index >(db);
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
