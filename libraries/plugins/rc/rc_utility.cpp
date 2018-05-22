
namespace steem { namespace plugins { namespace rc {

int64_t compute_rc_cost_of_resource(
   const rc_curve_params& curve_params,
   int64_t current_pool,
   int64_t resource_count )
{
   if( resource_count <= 0 )
   {
      if( resource_count < 0 )
         return -compute_rc_cost_of_resource( curve_params, current_pool, -resource_count );
      return 0;
   }
   uint128_t num = uint128_t( resource_count );
   num *= curve_params.coeff_a;
   uint128_t denom = uint128_t( curve_params.coeff_b );

   if( current_pool < 0 )
   {
      // Clamp B+x to 1 in case x is large and negative
      uint64_t sub = uint64_t(-current_pool);
      if( sub >= curve_params.coeff_b )
          sub = curve_params.coeff_b-1;
      denom -= sub;
   }
   else
      denom += current_pool;
   uint128_t num_denom = num / denom;
   // Clamp A / (B+x) - D to 1 in case D is too big
   if( num_denom <= curve_params.coeff_d )
      return 1;
   return num_denom - curve_params.coeff_d;
}

} } }
