extern "C" {
#ifdef HAVE_CONFIG_H
#       include <config.h>
#endif
#ifndef RXW_DEBUG
#       define PGM_DISABLE_ASSERT
#endif
#include <impl/i18n.h>
#include <impl/framework.h>
#include <impl/rxw.h>
}

#ifdef _MSC_VER
#pragma warning( disable : 4291 )
#endif

namespace {

pgm_rxw_state_t& rxw_state( pgm_sk_buff_t &skb ) {
  return *(pgm_rxw_state_t *) skb.cb;
}
int& pkt_state( pgm_sk_buff_t &skb ) {
  return rxw_state( skb ).pkt_state;
}

int32_t diff_ui( uint32_t x,  uint32_t y ) { return (int32_t) x - (int32_t) y; }
bool    is_lt  ( uint32_t x,  uint32_t y ) { return diff_ui( x, y ) < 0; }
bool    is_lte ( uint32_t x,  uint32_t y ) { return diff_ui( x, y ) <= 0; }
bool    is_gt  ( uint32_t x,  uint32_t y ) { return diff_ui( x, y ) > 0; }
bool    is_gte ( uint32_t x,  uint32_t y ) { return diff_ui( x, y ) >= 0; }

struct Rxw : public pgm_rxw_t {
  void * operator new( size_t, void *ptr ) { return ptr; }
  void operator delete( void *ptr ) { pgm_free( ptr ); }

  Rxw( const pgm_tsi_t*const tsi,
       const uint16_t tpdu_size,
       const size_t sqns,
       const unsigned secs,
       const ssize_t max_rte,
       const uint32_t ack_c_p ) { /* zero filled */
    this->tsi            = tsi;
    this->max_tpdu       = tpdu_size;
    this->lead           = -1;
    this->trail          = 0;
    this->is_constrained = true;
    this->tg_size        = 1;
    this->ack_c_p        = pgm_fp16( ack_c_p );
    this->bitmap         = 0xffffffff;
    this->resize( sqns );
  }
  ~Rxw() {
    if ( this->pdata != NULL )
      pgm_free( this->pdata );
  }
  uint32_t length( void ) const     { return diff_ui( this->lead + 1,
                                                      this->trail ); }
  bool     is_empty( void ) const   { return this->length() == 0; }
  bool     is_full( void ) const    { return this->length() == this->alloc; }
  uint32_t next_lead ( void ) const { return this->lead + 1; }

  uint32_t tg_sqn( const uint32_t sequence ) const {
    const uint32_t tg_sqn_mask = 0xffffffff << this->tg_sqn_shift;
    return sequence & tg_sqn_mask;
  }
  uint32_t tg_pkt_sqn( const uint32_t sequence ) const {
    const uint32_t tg_sqn_mask = 0xffffffff << this->tg_sqn_shift;
    return sequence & ~tg_sqn_mask;
  }
  bool     is_first_of_tg_sqn( const uint32_t sequence ) const {
    return this->tg_pkt_sqn( sequence ) == 0;
  }
  bool     is_last_of_tg_sqn( const uint32_t sequence ) const {
    return this->tg_pkt_sqn( sequence ) == this->tg_size - 1;
  }
  bool     is_apdu_lost( pgm_sk_buff_t &skb ) {
    if ( pkt_state( skb ) == PGM_PKT_STATE_LOST_DATA )
      return true;
    if ( skb.pgm_opt_fragment == NULL )
      return false;
    uint32_t first_sqn = pgm_ntohl( skb.of_apdu_first_sqn );
    if ( first_sqn == skb.sequence )
      return false;
    pgm_sk_buff_t * first_skb = this->peek( first_sqn );
    if ( first_skb == NULL )
      return true;
    if ( pkt_state( *first_skb ) == PGM_PKT_STATE_LOST_DATA )
      return true;
    return false;
  }
  pgm_sk_buff_t *&pkt( uint32_t sequence ) {
    return this->pdata[ sequence & ( this->alloc - 1 ) ];
  }
  void put_pkt( pgm_sk_buff_t *skb ) {
    this->pdata[ skb->sequence & ( this->alloc - 1 ) ] = skb;
  }
  pgm_sk_buff_t *peek( uint32_t sequence ) {
    if ( is_gte( sequence, this->trail ) && is_lte( sequence, this->lead ) )
      return this->pkt( sequence );
    return NULL;
  }
  void clear( void );
  void start( const uint32_t sqn );
  void resize( size_t len );
  void unlink( pgm_sk_buff_t &skb );
  void new_state( pgm_sk_buff_t &skb,  const int skb_state );
  int check_valid( pgm_sk_buff_t &skb );
  int check_parity_valid( pgm_sk_buff_t &skb );
  int add( pgm_sk_buff_t& skb,  const pgm_time_t now,
           const pgm_time_t nak_rb_expiry );
  int add_data( pgm_sk_buff_t &skb,  const pgm_time_t now,
                const pgm_time_t nak_rb_expiry );
  int append( pgm_sk_buff_t &skb,  const pgm_time_t now );
  pgm_sk_buff_t * find_missing( uint32_t tg_sqn );
  int insert( pgm_sk_buff_t &new_skb );
  void update_insert_stats( const pgm_time_t tstamp,  pgm_sk_buff_t &skb );
  void remove_commit( void );
  ssize_t readv( pgm_msgv_t** pmsg,  const size_t pmglen );
  ssize_t readv_tg( pgm_msgv_t** pmsg,  const size_t pmglen );
  uint32_t adjust_window( void );
  uint32_t update( const uint32_t txw_lead,  const uint32_t txw_trail,
                   const pgm_time_t now,  const pgm_time_t nak_rb_expiry );
  void update_trail( const uint32_t txw_trail );
  void update_fec( const uint8_t rs_k );
  void add_placeholder( const pgm_time_t now,  const pgm_time_t nak_rb_expiry );
  uint32_t update_lead( const uint32_t txw_lead,  const pgm_time_t now,
                        const pgm_time_t nak_rb_expiry );
  int confirm( const uint32_t sequence,  const pgm_time_t now,
               const pgm_time_t nak_rdata_expiry,
               const pgm_time_t nak_rb_expiry );
  void lost( const uint32_t sequence ) {
    pgm_sk_buff_t * skb = this->peek( sequence );
    if ( skb != NULL ) this->lost( *skb );
  }
  bool lost( pgm_sk_buff_t &skb );
 #if 0 
  int add_parity( pgm_sk_buff_t &skb,  const pgm_time_t now,
                  const pgm_time_t nak_rb_expiry );
  void add_tg_placeholder( const pgm_time_t now,
                           const pgm_time_t nak_rb_expiry );
  int add_tg_placeholder_range( const uint32_t sequence,  const pgm_time_t now,
                                const pgm_time_t nak_rb_expiry );
#endif
};

void
Rxw::clear( void )
{
  for ( uint32_t i = 0; i < this->alloc; i++ ) {
    pgm_sk_buff_t *skb = this->pdata[ i ];
    if ( skb != NULL ) {
      this->unlink( *skb );
      pgm_free_skb( skb );
      this->pdata[ i ] = NULL;
    }
  }
}

void
Rxw::start( const uint32_t sqn )
{
  this->lead =
  this->rxw_lead = sqn;

  this->commit_lead =
  this->rxw_trail =
  this->rxw_trail_init =
  this->trail = sqn + 1;

  this->is_constrained =
  this->is_defined = true;
}

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
Rxw::resize( size_t len )
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
Rxw::unlink( pgm_sk_buff_t &skb )
{
  int skb_state = pkt_state( skb );
  if ( skb_state == PGM_PKT_STATE_ERROR )
    return;

  pkt_state( skb ) = PGM_PKT_STATE_ERROR;
  switch ( skb_state ) {
    case PGM_PKT_STATE_BACK_OFF:
    case PGM_PKT_STATE_WAIT_NCF:
    case PGM_PKT_STATE_WAIT_DATA: {
      pgm_queue_t &queue =
        ( skb_state == PGM_PKT_STATE_BACK_OFF ) ? this->nak_backoff_queue :
        ( skb_state == PGM_PKT_STATE_WAIT_NCF ) ? this->wait_ncf_queue :
                    /* PGM_PKT_STATE_WAIT_DATA */ this->wait_data_queue;
      pgm_assert( ! pgm_queue_is_empty( &queue ) );
      pgm_queue_unlink( &queue, (pgm_list_t *) &skb );
      break;
    }

    case PGM_PKT_STATE_HAVE_DATA:
    case PGM_PKT_STATE_HAVE_PARITY:
    case PGM_PKT_STATE_COMMIT_DATA:
    case PGM_PKT_STATE_LOST_DATA: {
      uint32_t &cnt =
        ( skb_state == PGM_PKT_STATE_HAVE_DATA )   ? this->fragment_count :
        ( skb_state == PGM_PKT_STATE_HAVE_PARITY ) ? this->parity_count :
        ( skb_state == PGM_PKT_STATE_COMMIT_DATA ) ? this->committed_count :
                    /* PGM_PKT_STATE_LOST_DATA */    this->lost_count;
      pgm_assert_cmpuint( cnt, >, 0 );
      cnt -= 1;
      break;
    }
    default:
      pgm_assert_not_reached();
      break;
  }
}

void
Rxw::new_state( pgm_sk_buff_t &skb,  const int skb_state )
{
  if ( pkt_state( skb ) != PGM_PKT_STATE_ERROR )
    this->unlink( skb );

  pkt_state( skb ) = skb_state;
  switch ( skb_state ) {
    case PGM_PKT_STATE_BACK_OFF:
    case PGM_PKT_STATE_WAIT_NCF:
    case PGM_PKT_STATE_WAIT_DATA: {
      pgm_queue_t &queue =
        ( skb_state == PGM_PKT_STATE_BACK_OFF ) ? this->nak_backoff_queue :
        ( skb_state == PGM_PKT_STATE_WAIT_NCF ) ? this->wait_ncf_queue :
                    /* PGM_PKT_STATE_WAIT_DATA */ this->wait_data_queue;
      pgm_queue_push_head_link( &queue, (pgm_list_t *) &skb );
      break;
    }

    case PGM_PKT_STATE_LOST_DATA:
      this->has_event = 1;
      this->cumulative_losses++;
      /* fall thru */
    case PGM_PKT_STATE_HAVE_DATA:
    case PGM_PKT_STATE_HAVE_PARITY:
    case PGM_PKT_STATE_COMMIT_DATA: {
      uint32_t &cnt =
        ( skb_state == PGM_PKT_STATE_HAVE_DATA )   ? this->fragment_count :
        ( skb_state == PGM_PKT_STATE_HAVE_PARITY ) ? this->parity_count :
        ( skb_state == PGM_PKT_STATE_COMMIT_DATA ) ? this->committed_count :
                    /* PGM_PKT_STATE_LOST_DATA */    this->lost_count;
      cnt += 1;
      break;
    }
    case PGM_PKT_STATE_ERROR:
      break;
    default:
      pgm_assert_not_reached();
      break;
  }
}

int
Rxw::check_valid( pgm_sk_buff_t &skb )
{
  if ( skb.len != pgm_ntohs( skb.pgm_header->pgm_tsdu_length ) )
    return PGM_RXW_MALFORMED;

  if ( is_lt( skb.sequence, pgm_ntohl( skb.pgm_data->data_trail ) ) )
    return PGM_RXW_BOUNDS;

  if ( ( skb.pgm_header->pgm_options & PGM_OPT_PARITY ) == 0 &&
       skb.pgm_opt_fragment != NULL ) {

    if ( pgm_ntohl( skb.of_apdu_len ) == skb.len )
      skb.pgm_opt_fragment = NULL;
    else {
      if ( pgm_ntohl( skb.of_apdu_len ) < skb.len ||
           is_gt( pgm_ntohl( skb.of_apdu_first_sqn ), skb.sequence ) ||
           pgm_ntohl( skb.of_apdu_len ) > PGM_MAX_APDU )
        return PGM_RXW_MALFORMED;
    }
  }
  return 0;
}

int
Rxw::check_parity_valid( pgm_sk_buff_t &skb )
{
  if ( ! this->is_fec_available )
    return 0;

  uint32_t first_sqn = this->tg_pkt_sqn( skb.sequence );
  if ( first_sqn == skb.sequence )
    return 0;
  pgm_sk_buff_t * first_skb = this->peek( first_sqn );
  bool skb_has_payload, first_has_payload;

  if ( ( skb.pgm_header->pgm_options & PGM_OPT_VAR_PKTLEN ) != 0 ) {
    if ( first_skb == NULL )
      return PGM_RXW_MISSING;
    if ( first_skb->len != skb.len )
      return PGM_RXW_MALFORMED;
  }
  skb_has_payload   = ( skb.pgm_opt_fragment != NULL &&
                        skb.pgm_header->pgm_options & PGM_OP_ENCODED );
  first_has_payload = ( first_skb != NULL &&
                        first_skb->pgm_opt_fragment != NULL &&
                      first_skb->pgm_header->pgm_options & PGM_OP_ENCODED );
  if ( skb_has_payload != first_has_payload )
    return PGM_RXW_MISSING;
  return 0;
}

int
Rxw::add( pgm_sk_buff_t &skb,  const pgm_time_t now,
          const pgm_time_t nak_rb_expiry )
{
  int status;
  
  skb.sequence = pgm_ntohl( skb.pgm_data->data_sqn );
  if ( (status = this->check_valid( skb )) != 0 )
    return status;

  if ( ! this->is_defined )
    this->start( skb.sequence - 1 );
  else
    this->update_trail( pgm_ntohl( skb.pgm_data->data_trail ) );

  if ( ( skb.pgm_header->pgm_options & PGM_OPT_PARITY ) != 0 ) {
    if ( (status = this->check_parity_valid( skb )) != 0 )
      return status;
  }
  return this->add_data( skb, now, nak_rb_expiry );
}

int
Rxw::add_data( pgm_sk_buff_t &skb,  const pgm_time_t now,
               const pgm_time_t nak_rb_expiry )
{
  int status;

  if ( is_lt( skb.sequence, this->commit_lead ) ) {
    if ( is_gte( skb.sequence, this->trail ) )
      return PGM_RXW_DUPLICATE;
    else
      return PGM_RXW_BOUNDS;
  }
  if ( is_lte( skb.sequence, this->lead ) ) {
    this->has_event = 1;
    return this->insert( skb );
  }
  if ( skb.sequence == this->next_lead() ) {
    this->has_event = 1;
    rxw_state( skb ).is_contiguous = 1;
    return this->append( skb, now );
  }
  /* missing sequences */
  do {
    this->add_placeholder( now, nak_rb_expiry );
  } while ( this->next_lead() != skb.sequence );
  status = this->append( skb, now );
  if ( status == PGM_RXW_APPENDED )
    status = PGM_RXW_MISSING;
  return status;
}

void
Rxw::add_placeholder( const pgm_time_t now,  const pgm_time_t nak_rb_expiry )
{
  if ( this->is_full() )
    this->adjust_window();
  this->lead++;
  this->bitmap <<= 1; /* add loss to bitmap */
  /* update the Exponential Moving Average (EMA) data loss with loss:
   *     s_t = α × x_{t-1} + (1 - α) × s_{t-1}
   * x_{t-1} = 1
   *   ∴ s_t = α + (1 - α) × s_{t-1}
   */
  this->data_loss = this->ack_c_p +
      pgm_fp16mul( ( pgm_fp16( 1 ) - this->ack_c_p ), this->data_loss );

  pgm_sk_buff_t *skb = pgm_alloc_skb( this->max_tpdu );
  skb->tstamp = now;
  skb->sequence = this->lead;
  rxw_state( *skb ).timer_expiry = nak_rb_expiry;

  /* add skb to window */
  this->put_pkt( skb );
  this->new_state( *skb, PGM_PKT_STATE_BACK_OFF );
}

int
Rxw::append( pgm_sk_buff_t &skb,  const pgm_time_t now )
{
  if ( this->is_full() )
    this->adjust_window();
  this->lead++;
  this->bitmap = ( this->bitmap << 1 ) | 1;
  this->data_loss = pgm_fp16mul( this->data_loss,
                                 pgm_fp16( 1 ) - this->ack_c_p );

  if ( skb.pgm_opt_fragment != NULL && this->is_apdu_lost( skb ) ) {
    pgm_sk_buff_t * lost_skb = pgm_alloc_skb( this->max_tpdu );
    lost_skb->tstamp   = now;
    lost_skb->sequence = skb.sequence;
    this->put_pkt( lost_skb );
    this->new_state( *lost_skb, PGM_PKT_STATE_LOST_DATA );
    return PGM_RXW_BOUNDS;
  }

  this->put_pkt( &skb );
  bool is_data = ( ( skb.pgm_header->pgm_options & PGM_OPT_PARITY ) == 0 );
  this->new_state( skb, is_data ? PGM_PKT_STATE_HAVE_DATA :
                                  PGM_PKT_STATE_HAVE_PARITY );
  this->size += skb.len;
  return PGM_RXW_APPENDED;
}

int
Rxw::insert( pgm_sk_buff_t &new_skb )
{
  pgm_sk_buff_t * skb, * missing;

  if ( ( new_skb.pgm_header->pgm_options & PGM_OPT_PARITY ) == 0 ) {
    skb = this->peek( new_skb.sequence );
    pgm_assert( skb != NULL );
    if ( pkt_state( *skb ) == PGM_PKT_STATE_HAVE_DATA )
      return PGM_RXW_DUPLICATE;
  }
  else {
    skb = this->find_missing( new_skb.sequence );
    if ( skb == NULL )
      return PGM_RXW_DUPLICATE;
  }

  if ( new_skb.pgm_opt_fragment != NULL && this->is_apdu_lost( new_skb ) ) {
    this->lost( skb->sequence );
    return PGM_RXW_BOUNDS;
  }

  switch ( pkt_state( *skb ) ) {
    case PGM_PKT_STATE_BACK_OFF:
    case PGM_PKT_STATE_WAIT_NCF:
    case PGM_PKT_STATE_WAIT_DATA:
    case PGM_PKT_STATE_LOST_DATA:
      break;

    case PGM_PKT_STATE_HAVE_PARITY:
      missing = this->find_missing( skb->sequence );
      if ( missing != NULL ) {
        pgm_rxw_state_t tmp   = rxw_state( *skb );
        rxw_state( *skb )     = rxw_state( *missing );
        rxw_state( *missing ) = tmp;
        this->put_pkt( skb );
        this->put_pkt( missing );
      }
      break;

    default:
      pgm_assert_not_reached();
      break;
  }
  this->update_insert_stats( new_skb.tstamp, *skb );

  const uint32_t pos = diff_ui( this->lead, new_skb.sequence );
  if ( pos < 32 )
    this->bitmap |= 1U << pos;

  const uint32_t s = pgm_fp16pow( pgm_fp16( 1 ) - this->ack_c_p, pos );
  this->data_loss = ( s >= this->data_loss ) ? 0 : this->data_loss - s;

  rxw_state( new_skb ) = rxw_state( *skb );
  pkt_state( new_skb ) = PGM_PKT_STATE_ERROR;
  this->unlink( *skb );
  pgm_free_skb( skb );
  this->put_pkt( &new_skb );
  bool is_data = ( ( new_skb.pgm_header->pgm_options & PGM_OPT_PARITY ) == 0 );
  this->new_state( new_skb, is_data ? PGM_PKT_STATE_HAVE_DATA :
                                      PGM_PKT_STATE_HAVE_PARITY );
  this->size += new_skb.len;

  return PGM_RXW_INSERTED;
}

void
Rxw::update_insert_stats( const pgm_time_t tstamp,  pgm_sk_buff_t &skb )
{
  const uint32_t fill_time = (uint32_t) ( tstamp - skb.tstamp )

  PGM_HISTOGRAM_TIMES("Rx.RepairTime", fill_time);
  PGM_HISTOGRAM_COUNTS("Rx.NakTransmits", rxw_state( skb ).nak_transmit_count);
  PGM_HISTOGRAM_COUNTS("Rx.NcfRetries", rxw_state( skb ).ncf_retry_count);
  PGM_HISTOGRAM_COUNTS("Rx.DataRetries", rxw_state( skb ).data_retry_count);

  if ( this->max_fill_time == 0 ) {
    this->max_fill_time = fill_time;
    this->min_fill_time = fill_time;
  }
  else {
    if ( fill_time > this->max_fill_time )
      this->max_fill_time = fill_time;
    else if ( fill_time < this->min_fill_time )
      this->min_fill_time = fill_time;

    uint32_t xmit_cnt = rxw_state( skb ).nak_transmit_count;
    if ( this->max_nak_transmit_count == 0 ) {
      this->max_nak_transmit_count = xmit_cnt;
      this->min_nak_transmit_count = xmit_cnt;
    }
    else if ( xmit_cnt > this->max_nak_transmit_count )
      this->max_nak_transmit_count = xmit_cnt;
    else if ( xmit_cnt < this->min_nak_transmit_count )
      this->min_nak_transmit_count = xmit_cnt;
  }
}

ssize_t
Rxw::readv( pgm_msgv_t** pmsg,  const size_t pmglen )
{
  size_t          bytes_read = 0,
                  msgs_read  = 0, i;
  uint32_t        apdu_len,
                  first_sqn, j;
  pgm_sk_buff_t * skb;
  pgm_msgv_t    * msg = *pmsg;

  for ( i = 0; i < pmglen; i++ ) {
  next_pkt:;
    skb = this->peek( this->commit_lead );
    if ( skb == NULL )
      break;
    switch ( pkt_state( *skb ) ) {
      case PGM_PKT_STATE_HAVE_DATA:
        break;
      case PGM_PKT_STATE_LOST_DATA:
        if ( bytes_read != 0 )
          goto break_loop;
        this->commit_lead++;
        goto next_pkt;
      case PGM_PKT_STATE_COMMIT_DATA:
      case PGM_PKT_STATE_ERROR:
        pgm_assert_not_reached();
      default: /* fall thru */
        goto break_loop;
    }
    pgm_msgv_t & m = msg[ i ];
    m.msgv_skb[ 0 ] = skb;
    m.msgv_len = 1;
    /* no frags */
    if ( skb->pgm_opt_fragment == NULL ) {
      bytes_read += skb->len;
      msgs_read++;
      this->commit_lead++;
      this->new_state( *skb, PGM_PKT_STATE_COMMIT_DATA );
      continue;
    }
    /* fragmented message */
    first_sqn = pgm_ntohl( skb->of_apdu_first_sqn );
    if ( first_sqn != skb->sequence ) { /* frags lost */
      if ( bytes_read == 0 ) {
        this->lost( *skb );
        this->commit_lead++;
      }
      goto break_loop;
    }
    apdu_len = pgm_ntohl( skb->of_apdu_len );
    for (;;) {
      if ( apdu_len <= skb->len )
        break;
      apdu_len -= skb->len;
      skb = this->peek( this->commit_lead + m.msgv_len );
      if ( skb == NULL )
        goto break_loop;
      switch ( pkt_state( *skb ) ) {
        case PGM_PKT_STATE_HAVE_DATA:
          break;
        case PGM_PKT_STATE_LOST_DATA:
          if ( bytes_read != 0 )
            goto break_loop;
          this->commit_lead++;
          goto next_pkt;
          /* fall through */
        default:
          goto break_loop;
      }
      if ( m.msgv_len < PGM_MAX_FRAGMENTS )
        m.msgv_skb[ m.msgv_len ] = skb;
      m.msgv_len++;
    }
    /* commit all */
    if ( m.msgv_len <= PGM_MAX_FRAGMENTS ) {
      for ( j = 0; j < m.msgv_len; j++ ) {
        skb = m.msgv_skb[ j ];
        bytes_read += skb->len;
        msgs_read++;
        this->new_state( *skb, PGM_PKT_STATE_COMMIT_DATA );
      }
      this->commit_lead += m.msgv_len;
    }
    /* lose fragments, too many of them */
    else {
      for ( j = 0; j < m.msgv_len; j++ ) {
        skb = this->peek( this->commit_lead + j );
        this->lost( *skb );
      }
      break;
    }
  }
break_loop:;
  if ( i < pmglen ) {
    msg[ i ].msgv_skb[ 0 ] = NULL;
    msg[ i ].msgv_len = 0;
  }
  if ( bytes_read > 0 ) {
    *pmsg = &msg[ i ];
    this->bytes_delivered += (uint32_t) bytes_read;
    this->msgs_delivered  += (uint32_t) msgs_read;
    return bytes_read;
  }
  return -1;
}

ssize_t
Rxw::readv_tg( pgm_msgv_t** pmsg,  const size_t pmglen )
{
  return this->readv( pmsg, pmglen );
}

void
Rxw::remove_commit( void )
{
  this->adjust_window();
}

uint32_t
Rxw::adjust_window( void )
{
  while ( is_lt( this->trail, this->commit_lead ) ) {
    pgm_sk_buff_t *skb = this->pkt( this->trail );
    this->unlink( *skb );
    this->size -= skb->len;
    pgm_free_skb( skb );
    this->pkt( this->trail++ ) = NULL;
  }
  while ( is_lt( this->commit_lead, this->rxw_trail ) &&
          is_lte( this->commit_lead, this->lead ) ) {
    pgm_sk_buff_t *skb = this->pkt( this->commit_lead );
    if ( pkt_state( *skb ) >= PGM_PKT_STATE_HAVE_DATA )
      break;
    if ( ! this->lost( *skb ) )
      break;
    this->commit_lead++;
  }
  if ( this->is_full() ) {
    size_t len = this->alloc * 2;
    if ( is_gt( this->rxw_lead, this->rxw_trail ) ) {
      int32_t len2 = diff_ui( this->rxw_lead + 1, this->rxw_trail );
      if ( len2 > (int32_t) len )
        len = len2;
    }
    this->resize( len );
  }
  return 0;
}

uint32_t
Rxw::update( const uint32_t txw_lead,  const uint32_t txw_trail,
             const pgm_time_t now,  const pgm_time_t nak_rb_expiry )
{
  if ( ! this->is_defined )
    this->start( txw_lead );

  this->update_trail( txw_trail );
  return this->update_lead( txw_lead, now, nak_rb_expiry );
}

uint32_t
Rxw::update_lead( const uint32_t txw_lead,  const pgm_time_t now,
                  const pgm_time_t nak_rb_expiry )
{
  if ( is_gt( txw_lead, this->rxw_lead ) )
    this->rxw_lead = txw_lead;
  if ( is_lte( txw_lead, this->lead ) )
    return 0;
  for ( uint32_t lost_cnt = 0; ; lost_cnt++ ) {
    if ( this->lead == txw_lead )
      return lost_cnt;
    this->add_placeholder( now, nak_rb_expiry );
  }
}

void
Rxw::update_trail( const uint32_t txw_trail )
{
  /* sections of the receive window:
   * 
   *  |     Commit       |   Incoming   |
   *  |<---------------->|<------------>|
   *  |                  |              |
   * trail         commit-lead        lead
   *
   * commit buffers are currently held by the application, the window trail
   * cannot be advanced if packets remain in the commit buffer.
   *
   * incoming buffers are waiting to be passed to the application.
   */
  if ( is_lte( txw_trail, this->rxw_trail ) )
    return;
  /* current peer trail is greater than last peer trail */
  this->rxw_trail = txw_trail;

  if ( this->is_constrained ) {
    if ( is_gt( txw_trail, this->rxw_trail_init ) )
      this->is_constrained = false;
  }
  /* already consumed the pkts */
  if ( is_lte( txw_trail, this->trail ) )
    return;
  /* check if missing data from old peer trail to new peer trail */
  if ( this->is_empty() ) { /* 1 + lead == trail */
    const uint32_t distance = diff_ui( txw_trail, this->trail );
    this->trail       = txw_trail;
    this->commit_lead = txw_trail;
    this->lead        = txw_trail - 1; /* still empty */

    /* add loss to bitmap */
    this->bitmap = ( distance < 32 ) ? ( this->bitmap << distance ) : 0;

    /* update the Exponential Moving Average (EMA) data loss with long jump:
     *  s_t = α × (p₁ + (1 - α) × p₂ + (1 - α)² × p₃ + ⋯)
     * omitting the weight by stopping after k terms,
     *      = α × ((1 - α)^^k + (1 - α)^^{k+1} +(1 - α)^^{k+1} + ⋯)
     *      = α × (1 - α)^^k × (1 + (1 - α) + (1 - α)² + ⋯)
     *      = (1 - α)^^k
     */
    this->data_loss = pgm_fp16mul( this->data_loss,
                   pgm_fp16pow( pgm_fp16( 1 ) - this->ack_c_p, distance ) );
    this->cumulative_losses += distance;
    return; /* emtpy, no need to check commit loss below */
  }
  /* lose data from commit-lead -> txw_trail, peer cant't repair it */
  for ( uint32_t sqn = this->commit_lead;
        is_gt( txw_trail, sqn ) && is_gte( this->lead, sqn ); sqn++ ) {
    pgm_sk_buff_t * skb = this->peek( sqn );

    switch ( pkt_state( *skb ) ) {
      case PGM_PKT_STATE_HAVE_DATA:
      case PGM_PKT_STATE_HAVE_PARITY:
      case PGM_PKT_STATE_LOST_DATA:
        break;

      case PGM_PKT_STATE_ERROR:
        pgm_assert_not_reached();

      default:
        this->lost( *skb );
        break;
    }
  }
}

int
Rxw::confirm( const uint32_t sequence,  const pgm_time_t now,
              const pgm_time_t nak_rdata_expiry,
              const pgm_time_t nak_rb_expiry )
{
 /* received a uni/multicast ncf, search for a matching nak & tag or extend
  * window if beyond lead
  *
  * returns:
  * PGM_RXW_BOUNDS - sequence is outside of window, or window is undefined.
  * PGM_RXW_UPDATED - receiver state updated, waiting for data.
  * PGM_RXW_DUPLICATE - data already exists at sequence.
  * PGM_RXW_APPENDED - lead is extended with state set waiting for data.
  */
  if ( ! this->is_defined )
    return PGM_RXW_BOUNDS;
  if ( is_lt( sequence, this->commit_lead ) ) {
    if ( is_gte( sequence, this->trail ) )
      return PGM_RXW_DUPLICATE;
    return PGM_RXW_BOUNDS;
  }
  /* if past lead, add sequences in wait state */
  int status = PGM_RXW_UPDATED;
  if ( is_gt( sequence, this->lead ) ) {
    do {
      this->add_placeholder( now, nak_rb_expiry );
    } while ( is_gt( sequence, this->lead ) );
    status = PGM_RXW_APPENDED;
  }

  pgm_sk_buff_t *skb = this->peek( sequence );
  pgm_assert( skb != NULL );
  switch ( pkt_state( *skb ) ) {
    case PGM_PKT_STATE_BACK_OFF:
    case PGM_PKT_STATE_WAIT_NCF:
      this->new_state( *skb, PGM_PKT_STATE_WAIT_DATA );
      /* fallthrough */

    case PGM_PKT_STATE_WAIT_DATA:
      rxw_state( *skb ).timer_expiry = nak_rdata_expiry;
      return status;

    case PGM_PKT_STATE_HAVE_DATA:
    case PGM_PKT_STATE_HAVE_PARITY:
    case PGM_PKT_STATE_COMMIT_DATA:
    case PGM_PKT_STATE_LOST_DATA:
      break;

    default:
      pgm_assert_not_reached();
      break;
  }
  return PGM_RXW_DUPLICATE;
}

bool
Rxw::lost( pgm_sk_buff_t &skb )
{
  switch ( pkt_state( skb ) ) {
    case PGM_PKT_STATE_BACK_OFF:
    case PGM_PKT_STATE_WAIT_NCF:
    case PGM_PKT_STATE_WAIT_DATA:
    case PGM_PKT_STATE_HAVE_DATA:
    case PGM_PKT_STATE_HAVE_PARITY:
      this->new_state( skb, PGM_PKT_STATE_LOST_DATA );
      return true;
    default:
      return false;
  }
}

pgm_sk_buff_t *
Rxw::find_missing( uint32_t tg_sqn )
{
  pgm_sk_buff_t * skb;
  for ( uint32_t i = 0; i < this->tg_size; i++ ) {
    skb = this->peek( tg_sqn + i );
    if ( skb != NULL ) {
      switch ( pkt_state( *skb ) )  {
        case PGM_PKT_STATE_BACK_OFF:
        case PGM_PKT_STATE_WAIT_NCF:
        case PGM_PKT_STATE_WAIT_DATA:
        case PGM_PKT_STATE_LOST_DATA:
          return skb;

        default:
          break;
      }
    }
  }
  return NULL;
}

void
Rxw::update_fec( const uint8_t rs_k )
{
  pgm_assert_cmpuint( rs_k, >, 1 );
  if ( this->is_fec_available ) {
    if ( rs_k == this->rs.k )
      return;
    pgm_rs_destroy( &this->rs );
  }
  else {
    this->is_fec_available = true;
  }
  pgm_rs_create( &this->rs, PGM_RS_DEFAULT_N, rs_k );
  this->tg_sqn_shift = pgm_power2_log2( rs_k );
  this->tg_size = this->rs.k;
}

#if 0
int
Rxw::add_parity( pgm_sk_buff_t &skb,  const pgm_time_t now,
                 const pgm_time_t nak_rb_expiry )
{
  uint32_t first_sqn = this->tg_sqn( skb.sequence );

  if ( is_lt( first_sqn, this->tg_sqn( this->commit_lead ) ) )
    return PGM_RXW_DUPLICATE;

  if ( is_lt( first_sqn, this->tg_sqn( this->lead ) ) ) {
    this->has_event = 1;
    return this->insert( skb );
  }
  pgm_sk_buff_t * first_skb = this->peek( first_sqn );

  if ( first_sqn == this->tg_sqn( this->lead ) ) {
    this->has_event = 1;
    if ( first_skb == NULL || rxw_state( *first_skb ).is_contiguous ) {
      rxw_state( skb ).is_contiguous = 1;
      return this->append( skb, now );
    }
    return this->insert( skb );
  }
  return this->add_placeholder_range( this->tg_sqn( skb.sequence ), now,
                                      nak_rb_expiry );
}

void
Rxw::add_tg_placeholder( const pgm_time_t now,  const pgm_time_t nak_rb_expiry )
{
  if ( this->is_full() )
    this->adjust_window();
  this->lead++;
  this->bitmap <<= 1; /* add loss to bitmap */
  /* update the Exponential Moving Average (EMA) data loss with loss:
   *     s_t = α × x_{t-1} + (1 - α) × s_{t-1}
   * x_{t-1} = 1
   *   ∴ s_t = α + (1 - α) × s_{t-1}
   */
  this->data_loss = this->ack_c_p +
      pgm_fp16mul( ( pgm_fp16( 1 ) - this->ack_c_p ), this->data_loss );

  pgm_sk_buff_t *skb = pgm_alloc_skb( this->max_tpdu );
  skb->tstamp = now;
  skb->sequence = this->lead;
  rxw_state( *skb ).timer_expiry = nak_rb_expiry;

  if ( ! this->is_first_of_tg_sqn( skb->sequence ) ) {
    pgm_sk_buff_t *first_skb =
        this->peek( this->tg_sqn( skb->sequence ) );
    if ( first_skb != NULL )
      rxw_state( *first_skb ).is_contiguous = 0;
  }
  /* add skb to window */
  this->put_pkt( skb );
  this->new_state( *skb, PGM_PKT_STATE_BACK_OFF );
}

int
Rxw::add_tg_placeholder_range( const uint32_t sequence,  const pgm_time_t now,
                               const pgm_time_t nak_rb_expiry )
{
  if ( is_lte( sequence, this->lead ) )
    return PGM_RXW_BOUNDS;
  while ( this->next_lead() != sequence )
    this->add_tg_placeholder( now, nak_rb_expiry );
  return PGM_RXW_APPENDED;
}
#endif
}

extern "C" {
PGM_GNUC_INTERNAL pgm_rxw_t*
pgm_rxw_create( const pgm_tsi_t*const tsi,
                const uint16_t tpdu_size,
                const unsigned sqns,
                const unsigned secs,
                const ssize_t max_rte,
                const uint32_t ack_c_p )
{
  size_t alloc_sqns = sqns ? sqns : (secs * max_rte) / tpdu_size;
  return new ( pgm_malloc0( sizeof( pgm_rxw_t ) ) )
         Rxw( tsi, tpdu_size, alloc_sqns, secs, max_rte, ack_c_p );
}

PGM_GNUC_INTERNAL void
pgm_rxw_destroy( pgm_rxw_t*const window )
{
  ((Rxw *) window)->clear();
  delete (Rxw *) window;
}

PGM_GNUC_INTERNAL int
pgm_rxw_add( pgm_rxw_t*const window,
             struct pgm_sk_buff_t*const skb,
             const pgm_time_t now,
             const pgm_time_t nak_rb_expiry )
{
  return ((Rxw *) window)->add( *skb, now, nak_rb_expiry );
}

PGM_GNUC_INTERNAL void
pgm_rxw_remove_commit (pgm_rxw_t*const window )
{
  ((Rxw *) window)->remove_commit();
}

PGM_GNUC_INTERNAL ssize_t
pgm_rxw_readv( pgm_rxw_t*const window,
               struct pgm_msgv_t** pmsg,
               const unsigned pmglen )
{
  if ( ! window->is_fec_available )
    return ((Rxw *) window)->readv( pmsg, pmglen );
  return ((Rxw *) window)->readv_tg( pmsg, pmglen );
}

PGM_GNUC_INTERNAL unsigned
pgm_rxw_remove_trail( pgm_rxw_t*const window )
{
  return ((Rxw *) window)->adjust_window();
}

PGM_GNUC_INTERNAL unsigned
pgm_rxw_update( pgm_rxw_t*const window,
                const uint32_t txw_lead,
                const uint32_t txw_trail,
                const pgm_time_t now,
                const pgm_time_t nak_rb_expiry )
{
  return ((Rxw *) window)->update( txw_lead, txw_trail, now, nak_rb_expiry );
}

PGM_GNUC_INTERNAL void
pgm_rxw_update_fec( pgm_rxw_t*const window,
                    const uint8_t rs_k )
{
  ((Rxw *) window)->update_fec( rs_k );
}

PGM_GNUC_INTERNAL int
pgm_rxw_confirm( pgm_rxw_t*const window,
                 const uint32_t sequence,
                 const pgm_time_t now,
                 const pgm_time_t nak_rdata_expiry,
                 const pgm_time_t nak_rb_expiry )
{
  return ((Rxw *) window)->confirm( sequence, now, nak_rdata_expiry,
                                    nak_rb_expiry );
}

PGM_GNUC_INTERNAL void
pgm_rxw_lost( pgm_rxw_t*const window,
              const uint32_t sequence )
{
  ((Rxw *) window)->lost( sequence );
}

PGM_GNUC_INTERNAL void
pgm_rxw_state( pgm_rxw_t*const window,
               struct pgm_sk_buff_t*const skb,
               const int new_pkt_state )
{
  ((Rxw *) window)->new_state( *skb, new_pkt_state );
}

PGM_GNUC_INTERNAL struct pgm_sk_buff_t*
pgm_rxw_peek( pgm_rxw_t*const window,
              const uint32_t sequence )
{
  return ((Rxw *) window)->peek( sequence );
}

PGM_GNUC_INTERNAL const char*
pgm_pkt_state_string( const int pkt_state )
{
  switch (pkt_state) {
    case PGM_PKT_STATE_BACK_OFF:    return "PGM_PKT_STATE_BACK_OFF";
    case PGM_PKT_STATE_WAIT_NCF:    return "PGM_PKT_STATE_WAIT_NCF";
    case PGM_PKT_STATE_WAIT_DATA:   return "PGM_PKT_STATE_WAIT_DATA";
    case PGM_PKT_STATE_HAVE_DATA:   return "PGM_PKT_STATE_HAVE_DATA";
    case PGM_PKT_STATE_HAVE_PARITY: return "PGM_PKT_STATE_HAVE_PARITY";
    case PGM_PKT_STATE_COMMIT_DATA: return "PGM_PKT_STATE_COMMIT_DATA";
    case PGM_PKT_STATE_LOST_DATA:   return "PGM_PKT_STATE_LOST_DATA";
    case PGM_PKT_STATE_ERROR:       return "PGM_PKT_STATE_ERROR";
    default:                        return "(unknown)";
  }
}

PGM_GNUC_INTERNAL const char*
pgm_rxw_returns_string (const int rxw_returns )
{
  switch (rxw_returns) {
    case PGM_RXW_OK:            return "PGM_RXW_OK";
    case PGM_RXW_INSERTED:      return "PGM_RXW_INSERTED";
    case PGM_RXW_APPENDED:      return "PGM_RXW_APPENDED";
    case PGM_RXW_UPDATED:       return "PGM_RXW_UPDATED";
    case PGM_RXW_MISSING:       return "PGM_RXW_MISSING";
    case PGM_RXW_DUPLICATE:     return "PGM_RXW_DUPLICATE";
    case PGM_RXW_MALFORMED:     return "PGM_RXW_MALFORMED";
    case PGM_RXW_BOUNDS:        return "PGM_RXW_BOUNDS";
    case PGM_RXW_SLOW_CONSUMER: return "PGM_RXW_SLOW_CONSUMER";
    case PGM_RXW_UNKNOWN:       return "PGM_RXW_UNKNOWN";
    default:                    return "(unknown)";
  }
}

PGM_GNUC_INTERNAL void
pgm_rxw_dump (const pgm_rxw_t*const window )
{
  pgm_info( "window = {"
          "tsi = {gsi = {identifier = %i.%i.%i.%i.%i.%i}, sport = %u}, "
          "nak_backoff_queue = {head = %p, tail = %p, length = %u}, "
          "wait_ncf_queue = {head = %p, tail = %p, length = %u}, "
          "wait_data_queue = {head = %p, tail = %p, length = %u}, "
          "lost_count = %u, "
          "fragment_count = %u, "
          "parity_count = %u, "
          "committed_count = %u, "
          "max_tpdu = %u, "
          "tg_size = %u, "
          "tg_sqn_shift = %u, "
          "lead = %u, "
          "trail = %u, "
          "rxw_trail = %u, "
          "rxw_trail_init = %u, "
          "commit_lead = %u, "
          "is_constrained = %u, "
          "is_defined = %u, "
          "has_event = %u, "
          "is_fec_available = %u, "
          "min_fill_time = %u, "
          "max_fill_time = %u, "
          "min_nak_transmit_count = %u, "
          "max_nak_transmit_count = %u, "
          "cumulative_losses = %u, "
          "bytes_delivered = %u, "
          "msgs_delivered = %u, "
          "size = %" PRIu64 ", "
          "alloc = %" PRIu64 ", "
          "pdata = []"
          "}",
          window->tsi->gsi.identifier[0], 
          window->tsi->gsi.identifier[1],
          window->tsi->gsi.identifier[2],
          window->tsi->gsi.identifier[3],
          window->tsi->gsi.identifier[4],
          window->tsi->gsi.identifier[5],
          pgm_ntohs (window->tsi->sport),

          (void*)window->nak_backoff_queue.head,
          (void*)window->nak_backoff_queue.tail,
          window->nak_backoff_queue.length,

          (void*)window->wait_ncf_queue.head,
          (void*)window->wait_ncf_queue.tail,
          window->wait_ncf_queue.length,

          (void*)window->wait_data_queue.head,
          (void*)window->wait_data_queue.tail,
          window->wait_data_queue.length,

          window->lost_count,
          window->fragment_count,
          window->parity_count,
          window->committed_count,
          window->max_tpdu,
          window->tg_size,
          window->tg_sqn_shift,
          window->lead,
          window->trail,
          window->rxw_trail,
          window->rxw_trail_init,
          window->commit_lead,
          window->is_constrained,
          window->is_defined,
          window->has_event,
          window->is_fec_available,
          window->min_fill_time,
          window->max_fill_time,
          window->min_nak_transmit_count,
          window->max_nak_transmit_count,
          window->cumulative_losses,
          window->bytes_delivered,
          window->msgs_delivered,
          (uint64_t) window->size,
          (uint64_t) window->alloc );
}
}
