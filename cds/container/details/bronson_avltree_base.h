//$$CDS-header$$

#ifndef CDSLIB_CONTAINER_DETAILS_BRONSON_AVLTREE_BASE_H
#define CDSLIB_CONTAINER_DETAILS_BRONSON_AVLTREE_BASE_H

#include <cds/container/details/base.h>
#include <cds/opt/compare.h>
#include <cds/urcu/options.h>
#include <cds/lock/spinlock.h>

namespace cds { namespace container {

    /// BronsonAVLTree related declarations
    namespace bronson_avltree {

        template <typename Key, typename T>
        struct node;

        //@cond
        template <typename Key, typename T, typename Lock>
        struct link
        {
            typedef node<Key, T> node_type;
            typedef uint32_t version_type;
            typedef Lock lock_type;

            enum
            {
                shrinking = 1,
                unlinked = 2,
                version_flags = shrinking | unlinked
                // the rest is version counter
            };

            atomics::atomic< int >          m_nHeight;  ///< Node height
            atomics::atomic<version_type>   m_nVersion; ///< Version bits
            atomics::atomic<node_type *>    m_pParent;  ///< Parent node
            atomics::atomic<node_type *>    m_pLeft;    ///< Left child
            atomics::atomic<node_type *>    m_pRight;   ///< Right child
            lock_type                       m_Lock;     ///< Node-level lock

            node_type *                     m_pNextRemoved; ///< thread-local list o removed node

            link()
                : m_nHeight( 0 )
                , m_nVersion( 0 )
                , m_pParent( nullptr )
                , m_pLeft( nullptr )
                , m_pRight( nullptr )
                , m_pNextRemoved( nullptr )
            {}

            link( int nHeight, version_type version, node_type * pParent, node_type * pLeft, node_type * pRight )
                : m_nHeight( nHeight )
                , m_nVersion( version )
                , m_pParent( pParent )
                , m_pLeft( pLeft )
                , m_pRight( pRight )
                , m_pNextRemoved( nullptr )
            {}

            atomics::atomic<node_type *>& child( int nDirection ) const
            {
                assert( nDirection != 0 );
                return nDirection < 0 ? m_pLeft : m_pRight;
            }

            void child( node_type * pChild, int nDirection, atomics::memory_order order )
            {
                assert( nDirection != 0 );
                if ( nDirection < 0 )
                    m_pLeft.store( pChild, order );
                else
                    m_pRight.store( pChild, order );
            }

            version_type version( atomics::memory_order order ) const
            {
                return m_nVersion.load( order );
            }

            void version( version_type ver, atomics::memory_order order )
            {
                m_nVersion.store( ver, order );
            }

            int height( atomics::memory_order order ) const
            {
                return m_nHeight.load( order );
            }

            void height( int h, atomics::memory_order order  )
            {
                m_nHeight.store( h, order );

            template <typename BackOff>
            void wait_until_shrink_completed( atomics::memory_order order ) const
            {
                BackOff bkoff;
                while ( is_shrinking( order ))
                    bkoff();
            }

            bool is_unlinked( atomics::memory_order order ) const
            {
                return (m_nVersion.load( order ) & unlinked) != 0;
            }

            bool is_shrinking( atomics::memory_order order ) const
            {
                return (m_nVersion.load( order ) & shrinking) != 0;
            }
        };

        template <typename Key, typename T, typename Lock>
        struct node : public link< Key, T, Lock >
        {
            typedef Key key_type;
            typedef T   mapped_type;
            typedef link< key_type, mapped_type, Lock > base_class;

            key_type const  m_key;
            atomics::atomic<mapped_type *>  m_pValue;

            template <typename Q>
            node( Q&& key )
                : m_key( std::forward<Q>(key) )
                , m_pValue( nullptr )
            {}

            template <typename Q>
            node( Q&& key, int nHeight, version_type version, node_type * pParent, node_type * pLeft, node_type * pRight )
                : base_class( nHeight, version, pParent, pLeft, pRight )
                , m_key( std::forward<Q>( key ) )
                , m_pValue( nullptr )
            {}

            T * value( atomics::memory_order order ) const
            {
                return m_pValue.load( order );
            }
        };
        //@endcond

        /// BronsonAVLTreeMap internal statistics
        template <typename Counter = cds::atomicity::event_counter>
        struct stat {
            typedef Counter   event_counter; ///< Event counter type

            event_counter   m_nFindSuccess; ///< Count of success \p find() call
            event_counter   m_nFindFailed;  ///< Count of failed \p find() call
            event_counter   m_nFindRetry;   ///< Count of retries during \p find()
            event_counter   m_nFindWaitShrinking;   ///< Count of waiting until shrinking completed duting \p find() call

            event_counter   m_nInsertSuccess;       ///< Count of inserting data node
            event_counter   m_nRelaxedInsertFailed; ///< Count of false creating of data nodes (only if @ref bronson_avltree::relaxed_insert "relaxed insertion" is enabled)
            event_counter   m_nInsertRetry;         ///< Count of insert retries via concurrent operations
            event_counter   m_nUpdateWaitShrinking; ///< Count of waiting until shrinking completed duting \p update() call
            event_counter   m_nUpdateRetry;         ///< Count of update retries via concurrent operations
            event_counter   m_nUpdateRootWaitShinking;  ///< Count of waiting until root shrinking completed duting \p update() call
            event_counter   m_nUpdateSuccess;       ///< Count of updating data node
            event_counter   m_nUpdateUnlinked;      ///< Count of updating of unlinked node attempts
            event_counter   m_nDisposedValue;       ///< Count of disposed value

            //@cond
            void onFindSuccess()        { ++m_nFindSuccess      ; }
            void onFindFailed()         { ++m_nFindFailed       ; }
            void onFindRetry()          { ++m_nFindRetry        ; }
            void onFindWaitShrinking()  { ++m_nFindWaitShrinking; }

            void onInsertSuccess()          { ++m_nInsertSuccess    ; }
            void onRelaxedInsertFailed()    { ++m_nRelaxedInsertFailed; }
            void onInsertRetry()            { ++m_nInsertRetry      ; }
            void onUpdateWaitShrinking()    { ++m_nUpdateWaitShrinking; }
            void onUpdateRetry()            { ++m_nUpdateRetry; }
            void onUpdateRootWaitShrinking() { ++m_nUpdateRootWaitShrinking; }
            void onUpdateSuccess()          { ++m_nUpdateSuccess;  }
            void onUpdateUnlinked()         { ++m_nUpdateUnlinked; }
            void onDisposeValue()           { ++m_nDisposedValue; }

            //@endcond
        };

        /// BronsonAVLTreeMap empty statistics
        struct empty_stat {
            //@cond
            void onFindSuccess()        const {}
            void onFindFailed()         const {}
            void onFindRetry()          const {}
            void onFindWaitShrinking()  const {}

            void onInsertSuccess()          const {}
            void onRelaxedInsertFailed()    const {}
            void onInsertRetry()            const {}
            void onUpdateWaitShrinking()    const {}
            void onUpdateRetry()            const {}
            void onUpdateRootWaitShrinking() const {}
            void onUpdateSuccess()          const {}
            void onUpdateUnlinked()         const {}
            void onDisposeValue()           const {}

            //@endcond
        };

        /// Option to allow relaxed insert into Bronson et al AVL-tree
        /**
            By default, this opton is disabled and the new node is created under its parent lock.
            In this case, it is guaranteed the new node will be attached to its parent.
            On the other hand, constructing of the new node can be too complex to make it under the lock,
            that can lead to lock contention.

            When this option is enabled, the new node is created before locking the parent node.
            After that, the parent is locked and checked whether the new node can be attached to the parent.
            In this case, false node creating can be performed, but locked section can be significantly small.
        */
        template <bool Enable>
        struct relaxed_insert {
            //@cond
            template <typename Base> struct pack : public Base
            {
                enum { relaxed_insert = Enable };
            };
            //@endcond
        };

        /// BronsnAVLTreeMap traits
        /**
            Note that there are two main specialization of Bronson et al AVL-tree:
            - pointer-oriented - the tree node stores an user-provided pointer to value: <tt>BronsonAVLTreeMap<GC, Key, T *, Traits> </tt>
            - data-oriented - the tree node contains a copy of values: <tt>BronsonAVLTreeMap<GC, Key, T, Traits> </tt> where \p T is not a pointer type.

            Depends on tree specialization, different traits member can be used.
        */
        struct traits
        {
            /// Key comparison functor
            /**
                No default functor is provided. If the option is not specified, the \p less is used.

                See \p cds::opt::compare option description for functor interface.

                You should provide \p compare or \p less functor.
            */
            typedef opt::none                       compare;

            /// Specifies binary predicate used for key compare.
            /**
                See \p cds::opt::less option description for predicate interface.

                You should provide \p compare or \p less functor.
            */
            typedef opt::none                       less;

            /// Allocator for internal node
            typedef CDS_DEFAULT_ALLOCATOR           allocator;

            /// Disposer (only for pointer-oriented tree specialization)
            /**
                The functor used for dispose removed values.
                The user-provided disposer is used only for pointer-oriented tree specialization
                like \p BronsonAVLTreeMap<GC, Key, T*, Traits>. When the node becomes the rounting node without value,
                the disposer will be called to signal that the memory for the value can be safely freed.
                Default is \ref cds::intrusive::opt::delete_disposer "cds::container::opt::v::delete_disposer<>" which calls \p delete operator.
            */
            typedef opt::v::delete_disposer<>       disposer;

            /// Node lock
            typedef std::mutex                      lock_type;

            /// Enable relaxed insertion.
            /**
                About relaxed insertion see \p bronson_avltree::relaxed_insert option.
                By default, this option is disabled.
            */
            static bool const relaxed_insert = false;

            /// Item counter
            /**
                The type for item counter, by default it is disabled (\p atomicity::empty_item_counter).
                To enable it use \p atomicity::item_counter
            */
            typedef atomicity::empty_item_counter     item_counter;

            /// C++ memory ordering model
            /**
                List of available memory ordering see \p opt::memory_model
            */
            typedef opt::v::relaxed_ordering        memory_model;

            /// Internal statistics
            /**
                By default, internal statistics is disabled (\p ellen_bintree::empty_stat).
                To enable it use \p ellen_bintree::stat.
            */
            typedef empty_stat                      stat;

            /// Back-off strategy
            typedef cds::backoff::empty             back_off;

            /// RCU deadlock checking policy (only for \ref cds_container_BronsonAVLTreeMap_rcu "RCU-based BronsonAVLTree")
            /**
                List of available options see \p opt::rcu_check_deadlock
            */
            typedef cds::opt::v::rcu_throw_deadlock      rcu_check_deadlock;

            //@cond
            // Internal traits, not for direct usage
            typedef opt::none   node_type;
            //@endcond
        };

        /// Metafunction converting option list to BronsonAVLTreeMap traits
        /**
            Note that there are two main specialization of Bronson et al AVL-tree:
            - pointer-oriented - the tree node stores an user-provided pointer to value: <tt>BronsonAVLTreeMap<GC, Key, T *, Traits> </tt>
            - data-oriented - the tree node contains a copy of values: <tt>BronsonAVLTreeMap<GC, Key, T, Traits> </tt> where \p T is not a pointer type.

            Depends on tree specialization, different options can be specified.

            \p Options are:
            - \p opt::compare - key compare functor. No default functor is provided.
                If the option is not specified, \p %opt::less is used.
            - \p opt::less - specifies binary predicate used for key compare. At least \p %opt::compare or \p %opt::less should be defined.
            - \p opt::allocator - the allocator for internal nodes. Default is \ref CDS_DEFAULT_ALLOCATOR.
            - \ref cds::intrusive::opt::disposer "container::opt::disposer" - the functor used for dispose removed values.
                The user-provided disposer is used only for pointer-oriented tree specialization
                like \p BronsonAVLTreeMap<GC, Key, T*, Traits>. When the node becomes the rounting node without value,
                the disposer will be called to signal that the memory for the value can be safely freed.
                Default is \ref cds::intrusive::opt::delete_disposer "cds::container::opt::v::delete_disposer<>" which calls \p delete operator.
                Due the nature of GC schema the disposer may be called asynchronously.
            - \p opt::lock_type - node lock type, default is \p std::mutex
            - \p bronson_avltree::relaxed_insert - enable (\p true) or disable (\p false, the default) 
                @ref bronson_avltree::relaxed_insert "relaxed insertion"
            - \p opt::item_counter - the type of item counting feature, by default it is disabled (\p atomicity::empty_item_counter)
                To enable it use \p atomicity::item_counter
            - \p opt::memory_model - C++ memory ordering model. Can be \p opt::v::relaxed_ordering (relaxed memory model, the default)
                or \p opt::v::sequential_consistent (sequentially consisnent memory model).
            - \p opt::stat - internal statistics, by default it is disabled (\p bronson_avltree::empty_stat)
                To enable statistics use \p \p bronson_avltree::stat
            - \p opt::backoff - back-off strategy, by default no strategy is used (\p cds::backoff::empty)
            - \p opt::rcu_check_deadlock - a deadlock checking policy for RCU-based tree, default is \p opt::v::rcu_throw_deadlock
        */
        template <typename... Options>
        struct make_traits {
#   ifdef CDS_DOXYGEN_INVOKED
            typedef implementation_defined type ;   ///< Metafunction result
#   else
            typedef typename cds::opt::make_options<
                typename cds::opt::find_type_traits< traits, Options... >::type
                ,Options...
            >::type   type;
#   endif
        };


    } // namespace bronson_avltree

    // Forwards
    template < class GC, typename Key, typename T, class Traits = bronson_avltree::traits >
    class BronsonAVLTreeMap;

}} // namespace cds::container


#endif // #ifndef CDSLIB_CONTAINER_DETAILS_BRONSON_AVLTREE_BASE_H