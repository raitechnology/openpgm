extern "C" {
#ifdef HAVE_CONFIG_H
#       include <config.h>
#endif
}

#ifndef TXW_DEBUG
#       define PGM_DISABLE_ASSERT
#endif
#include <impl/i18n.h>
#include <impl/framework.h>
#include <impl/txw.h>


namespace {

pgm_txw_state_t& txw_state( pgm_sk_buff_t &skb ) {
  return *(pgm_txw_state_t *) skb.cb;
}
int32_t diff_ui( uint32_t x,  uint32_t y ) { return (int32_t) x - (int32_t) y; }
bool    is_lt  ( uint32_t x,  uint32_t y ) { return diff_ui( x, y ) < 0; }
bool    is_lte ( uint32_t x,  uint32_t y ) { return diff_ui( x, y ) <= 0; }
bool    is_gt  ( uint32_t x,  uint32_t y ) { return diff_ui( x, y ) > 0; }
bool    is_gte ( uint32_t x,  uint32_t y ) { return diff_ui( x, y ) >= 0; }

struct Txw : public pgm_txw_t {
  void * operator new( size_t, void *ptr ) { return ptr; }
  void operator delete( void *ptr ) { pgm_free( ptr ); }

  Txw( const pgm_tsi_t*const tsi,
       const uint16_t tpdu_size,
       const unsigned sqns,
       const unsigned secs,
       const ssize_t  max_rte,
       const bool     use_fec,
       const uint8_t  rs_n,
       const uint8_t  rs_k ) {
    this->tsi      = tsi;
    this->max_tpdu = tpdu_size;
    this->lead     = -1;
    this->trail    = 0;
    this->adv_secs = secs;
    this->adv_sqns = sqns ? sqns : (secs * max_rte) / tpdu_size;

    this->resize( this->adv_sqns );
  }
  ~Txw() {
    if ( this->pdata != NULL )
      pgm_free( this->pdata );
  }
  uint32_t length( void ) const     { return diff_ui( this->lead + 1,
                                                      this->trail ); }
  bool     is_empty( void ) const   { return this->length() == 0; }
  bool     is_full( void ) const    { return this->length() == this->alloc; }
  uint32_t next_lead ( void ) const { return this->lead + 1; }

  pgm_sk_buff_t *&pkt( uint32_t sequence ) {
    return this->pdata[ sequence & ( this->alloc - 1 ) ];
  }
  pgm_sk_buff_t *peek( uint32_t sequence ) {
    if ( is_gte( sequence, this->trail ) && is_lte( sequence, this->lead ) )
      return this->pkt( sequence );
    return NULL;
  }
  bool window_time_expired( const pgm_time_t now ) {
    if ( this->adv_secs == 0 )
      return true;
    pgm_sk_buff_t *skb = this->pkt( this->trail );
    if ( pgm_to_secs( now - txw_state( *skb ).add_tstamp ) > this->adv_secs )
      return true;
    return false;
  }
  void clear( void );
  void resize( size_t len );
  void remove_tail( void );
  void add( pgm_sk_buff_t* skb,  const pgm_time_t t );
  bool retransmit_push( const uint32_t sequence, const bool is_parity, const uint8_t tg_sqn_shift );
  pgm_sk_buff_t* retransmit_try_peek( void );
  void retransmit_remove_head( void );
};

size_t base2_sqn( size_t len ) {
  if ( len < 64 )
    return 64;
  if ( ( len & ( len - 1 ) ) == 0 )
    return len;
  for ( size_t n = len - 1; ; n >>= 1 ) {
    if ( n == 0 )
      return len + 1;
    len |= n;
  }
}

void
Txw::clear( void )
{
  while ( ! pgm_queue_is_empty( &this->retransmit_queue ) )
    this->remove_tail();

  for ( uint32_t i = 0; i < this->alloc; i++ ) {
    pgm_sk_buff_t *skb = this->pdata[ i ];
    if ( skb != NULL ) {
      pgm_free_skb( skb );
      this->pdata[ i ] = NULL;
    }
  }
}

void
Txw::resize( size_t len )
{
  size_t i, j, mask, size;
  pgm_sk_buff_t ** tmp = this->pdata,
                 * skb;
  len  = base2_sqn( len );
  size = len * sizeof( pgm_sk_buff_t * );
  mask = len - 1;

  if ( this->alloc > 0 && len > this->alloc ) {
    this->pdata = (pgm_sk_buff_t **) pgm_realloc( tmp, size );
    ::memset( &this->pdata[ this->alloc ], 0,
              sizeof( this->pdata[ 0 ] ) * ( len - this->alloc ) );
    for ( i = 0; i < this->alloc; i++ ) {
      if ( (skb = this->pdata[ i ]) != NULL ) {
        j = ( skb->sequence & mask );
        if ( i != j ) {
          this->pdata[ i ] = NULL;
          this->pdata[ j ] = skb;
        }
      }
    }
  }
  else {
    this->pdata = (pgm_sk_buff_t **) pgm_malloc0( size );
    if ( this->alloc > 0 ) {
      for ( i = 0; i < this->alloc; i++ ) {
        if ( (skb = tmp[ i ]) != NULL )
          this->pdata[ skb->sequence & mask ] = skb;
      }
      pgm_free( tmp );
    }
  }
  this->alloc = len;
}

void
Txw::remove_tail( void )
{
  pgm_assert( ! this->is_empty() );
  pgm_sk_buff_t *skb = this->pkt( this->trail );
  this->pkt( this->trail ) = NULL;
  if ( txw_state( *skb ).waiting_retransmit ) {
    txw_state( *skb ).waiting_retransmit = 0;
    pgm_queue_unlink( &this->retransmit_queue, (pgm_list_t *) skb );
  }
  this->size -= skb->len;
  PGM_HISTOGRAM_COUNTS("Tx.RetransmitCount", txw_state( *skb ).retransmit_count);
  PGM_HISTOGRAM_COUNTS("Tx.NakEliminationCount", txw_state( *skb ).nak_elimination_count);
  pgm_free_skb( skb );
  this->trail++;
}

void
Txw::add( pgm_sk_buff_t* skb,  const pgm_time_t now )
{
  if ( this->is_full() ) {
    if ( this->window_time_expired( now ) )
      this->remove_tail();
    else
      this->resize( this->alloc * 2 );
  }
  skb->sequence = ++this->lead;
  txw_state( *skb ).add_tstamp = now;
  this->pkt( skb->sequence ) = skb;
  this->size += skb->len;
}

bool
Txw::retransmit_push( const uint32_t sequence, const bool , const uint8_t )
{
  pgm_sk_buff_t * skb = this->pkt( sequence );
  if ( skb == NULL )
    return false;
  if ( txw_state( *skb ).waiting_retransmit ) {
    txw_state( *skb ).nak_elimination_count++;
    return false;
  }
  txw_state( *skb ).waiting_retransmit = 1;
  pgm_queue_push_head_link( &this->retransmit_queue, (pgm_list_t*) skb );
  return true;
}

pgm_sk_buff_t*
Txw::retransmit_try_peek( void )
{
  return (pgm_sk_buff_t*) pgm_queue_peek_tail_link( &this->retransmit_queue );
}

void
Txw::retransmit_remove_head( void )
{
  pgm_sk_buff_t * skb;
  skb = (pgm_sk_buff_t *) pgm_queue_pop_tail_link( &this->retransmit_queue );
  if ( skb != NULL )
    txw_state( *skb ).waiting_retransmit = 0;
}

}


PGM_GNUC_INTERNAL
pgm_txw_t *
pgm_txw_create( const pgm_tsi_t *const tsi, const uint16_t tpdu_size,
                const uint32_t sqns,   /* transmit window size in sequence numbers */
                const unsigned secs,   /* size in seconds */
                const ssize_t max_rte, /* max bandwidth */
                const bool use_fec, const uint8_t rs_n, const uint8_t rs_k )
{
  return new ( pgm_malloc0( sizeof( pgm_txw_t ) ) )
         Txw( tsi, tpdu_size, sqns, secs, max_rte, use_fec, rs_n, rs_k );
}

PGM_GNUC_INTERNAL
void
pgm_txw_shutdown( pgm_txw_t*const window )
{
  ((Txw *) window)->clear();
  delete (Txw *) window;
}

PGM_GNUC_INTERNAL
void
pgm_txw_add( pgm_txw_t*const window, struct pgm_sk_buff_t*const skb,  const pgm_time_t now )
{
  ((Txw *) window)->add( skb, now );
}

PGM_GNUC_INTERNAL
struct pgm_sk_buff_t*
pgm_txw_peek( const pgm_txw_t*const window, const uint32_t sequence )
{
  return ((Txw *) window)->peek( sequence );
}

PGM_GNUC_INTERNAL
bool
pgm_txw_retransmit_push( pgm_txw_t*const window, const uint32_t sequence, const bool is_parity,
                         const uint8_t tg_sqn_shift )
{
  return ((Txw *) window)->retransmit_push( sequence, is_parity, tg_sqn_shift );
}

PGM_GNUC_INTERNAL
struct pgm_sk_buff_t*
pgm_txw_retransmit_try_peek( pgm_txw_t*const window )
{
  return ((Txw *) window)->retransmit_try_peek();
}

PGM_GNUC_INTERNAL
void
pgm_txw_retransmit_remove_head( pgm_txw_t*const window )
{
  return ((Txw *) window)->retransmit_remove_head();
}

PGM_GNUC_INTERNAL
uint32_t
pgm_txw_get_unfolded_checksum( const struct pgm_sk_buff_t*const skb )
{
  return txw_state( (pgm_sk_buff_t &) *skb ).unfolded_checksum;
}

PGM_GNUC_INTERNAL
void
pgm_txw_set_unfolded_checksum( struct pgm_sk_buff_t*const skb, const uint32_t csum )
{
  txw_state( *skb ).unfolded_checksum = csum;
}

PGM_GNUC_INTERNAL
void
pgm_txw_inc_retransmit_count (struct pgm_sk_buff_t*const skb )
{
  txw_state( *skb ).retransmit_count++;
}

PGM_GNUC_INTERNAL
bool
pgm_txw_retransmit_is_empty( const pgm_txw_t*const window )
{
  return pgm_queue_is_empty( &window->retransmit_queue );
}
