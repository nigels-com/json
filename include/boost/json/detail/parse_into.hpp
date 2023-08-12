//
// Copyright (c) 2019 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2020 Krystian Stasiowski (sdkrystian@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/json
//

#ifndef BOOST_JSON_DETAIL_PARSE_INTO_HPP
#define BOOST_JSON_DETAIL_PARSE_INTO_HPP

#include <boost/json/detail/config.hpp>

#include <boost/json/error.hpp>
#include <boost/json/conversion.hpp>
#include <boost/describe/enum_from_string.hpp>

/*
 * This file contains the majority of parse_into functionality, specifically
 * the implementation of dedicated handlers for different generic categories of
 * types.
 *
 * At the core of parse_into is the specialisation basic_parser<
 * detail::into_handler<T> >. detail::into_handler<T> is a handler for
 * basic_parser. It directly handles events on_comment_part and on_comment (by
 * ignoring them), on_document_begin (by enabling the nested dedicated
 * handler), and on_document_end (by disabling the nested handler).
 *
 * Every other event is handled by the nested handler, which has the type
 * get_handler< T, into_handler<T> >. The second parameter is the parent
 * handler (in this case, it's the top handler, into_handler<T>). The type is
 * actually an alias to class template converting_handler, which has a separate
 * specialisation for every conversion category from the list of generic
 * conversion categories (e.g. sequence_conversion_tag, tuple_conversion_tag,
 * etc.) Instantiations of the template store a pointer to the parent handler
 * and a pointer to the value T.
 *
 * The nested handler handles specific parser events by setting error_code to
 * an appropriate value, if it receives an event it isn't supposed to handle
 * (e.g. a number handler getting an on_string event), and also updates the
 * value when appropriate. Note that they never need to handle on_comment_part,
 * on_comment, on_document_begin, and on_document_end events, as those are
 * always handled by the top handler into_handler<T>.
 *
 * When the nested handler receives an event that completes the current value,
 * it is supposed to call its parent's signal_value member function. This is
 * necessary for correct handling of composite types (e.g. sequences).
 *
 * Finally, nested handlers should always call parent's signal_end member
 * function if they don't handle on_array_end themselves. This is necessary
 * to correctly handle nested composites (e.g. sequences inside sequences).
 *
 * converting_handler instantiations for composite categories of types have
 * their own nested handlers, to which they themselves delegate events. For
 * complex types you will get a tree of handlers with into_handler<T> as the
 * root and handlers for scalars as leaves.
 *
 * To reiterate, only into_handler has to handle on_comment_part, on_comment,
 * on_document_begin, and on_document_end; only handlers for composites and
 * into_handler has to provide signal_value and signal_end; all handlers
 * except for into_handler have to call their parent's signal_end from
 * their on_array_begin, if they don't handle it themselves; once a handler
 * receives an event that finishes its current value, it should call its
 * parent's signal_value.
 */

namespace boost {
namespace json {
namespace detail {

template< class Impl, class T, class Parent >
class converting_handler;

// get_handler
template< class V, class P >
using get_handler = converting_handler< generic_conversion_category<V>, V, P >;

template<error E> class handler_error_base
{
public:

    handler_error_base() = default;

    handler_error_base( handler_error_base const& ) = delete;
    handler_error_base& operator=( handler_error_base const& ) = delete;

public:

    bool on_object_begin( error_code& ec ) { BOOST_JSON_FAIL( ec, E ); return false; }
    bool on_object_end( error_code& ec, std::size_t ) { BOOST_JSON_FAIL( ec, E ); return false; }
    bool on_array_begin( error_code& ec ) { BOOST_JSON_FAIL( ec, E ); return false; }
    bool on_array_end( error_code& ec, std::size_t ) { BOOST_JSON_FAIL( ec, E ); return false; }
    bool on_key_part( error_code& ec, string_view, std::size_t ) { BOOST_JSON_FAIL( ec, E ); return false; }
    bool on_key( error_code& ec, string_view, std::size_t ) { BOOST_JSON_FAIL( ec, E ); return false; }
    bool on_string_part( error_code& ec, string_view, std::size_t ) { BOOST_JSON_FAIL( ec, E ); return false; }
    bool on_string( error_code& ec, string_view, std::size_t ) { BOOST_JSON_FAIL( ec, E ); return false; }
    bool on_number_part( error_code& ec, string_view ) { BOOST_JSON_FAIL( ec, E ); return false; }
    bool on_int64( error_code& ec, std::int64_t, string_view ) { BOOST_JSON_FAIL( ec, E ); return false; }
    bool on_uint64( error_code& ec, std::uint64_t, string_view ) { BOOST_JSON_FAIL( ec, E ); return false; }
    bool on_double( error_code& ec, double, string_view ) { BOOST_JSON_FAIL( ec, E ); return false; }
    bool on_bool( error_code& ec, bool ) { BOOST_JSON_FAIL( ec, E ); return false; }
    bool on_null( error_code& ec ) { BOOST_JSON_FAIL( ec, E ); return false; }
};

template< class P, error E >
class scalar_handler
    : public handler_error_base<E>
{
protected:
    P* parent_;

public:
    scalar_handler(scalar_handler const&) = delete;
    scalar_handler& operator=(scalar_handler const&) = delete;

    scalar_handler(P* p): parent_( p )
    {}

    bool on_array_end( error_code&, std::size_t )
    {
        parent_->signal_end();
        return true;
    }
};

template< class D, class V, class P, error E >
class composite_handler
{
protected:
    using inner_handler_type = get_handler<V, D>;

    P* parent_;
#if defined(__GNUC__) && __GNUC__ < 5 && !defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
    V next_value_ = {};
    inner_handler_type inner_;
    bool inner_active_ = false;

public:
    composite_handler( composite_handler const& ) = delete;
    composite_handler& operator=( composite_handler const& ) = delete;

    composite_handler( P* p )
        : parent_(p), inner_( &next_value_, static_cast<D*>(this) )
    {}
#if defined(__GNUC__) && __GNUC__ < 5 && !defined(__clang__)
# pragma GCC diagnostic pop
#endif

    void signal_end()
    {
        inner_active_ = false;
        parent_->signal_value();
    }

#define BOOST_JSON_INVOKE_INNER(f) \
    if( !inner_active_ ) { \
        BOOST_JSON_FAIL(ec, E); \
        return false; \
    } \
    else \
        return inner_.f

    bool on_object_begin( error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_object_begin(ec) );
    }

    bool on_object_end( error_code& ec, std::size_t n )
    {
        BOOST_JSON_INVOKE_INNER( on_object_end(ec, n) );
    }

    bool on_array_begin( error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_array_begin(ec) );
    }

    bool on_array_end( error_code& ec, std::size_t n )
    {
        BOOST_JSON_INVOKE_INNER( on_array_end(ec, n) );
    }

    bool on_key_part( error_code& ec, string_view sv, std::size_t n )
    {
        BOOST_JSON_INVOKE_INNER( on_key_part(ec, sv, n) );
    }

    bool on_key( error_code& ec, string_view sv, std::size_t n )
    {
        BOOST_JSON_INVOKE_INNER( on_key(ec, sv, n) );
    }

    bool on_string_part( error_code& ec, string_view sv, std::size_t n )
    {
        BOOST_JSON_INVOKE_INNER( on_string_part(ec, sv, n) );
    }

    bool on_string( error_code& ec, string_view sv, std::size_t n )
    {
        BOOST_JSON_INVOKE_INNER( on_string(ec, sv, n) );
    }

    bool on_number_part( error_code& ec, string_view sv )
    {
        BOOST_JSON_INVOKE_INNER( on_number_part(ec, sv) );
    }

    bool on_int64( error_code& ec, std::int64_t v, string_view sv )
    {
        BOOST_JSON_INVOKE_INNER( on_int64(ec, v, sv) );
    }

    bool on_uint64( error_code& ec, std::uint64_t v, string_view sv )
    {
        BOOST_JSON_INVOKE_INNER( on_uint64(ec, v, sv) );
    }

    bool on_double( error_code& ec, double v, string_view sv )
    {
        BOOST_JSON_INVOKE_INNER( on_double(ec, v, sv) );
    }

    bool on_bool( error_code& ec, bool v )
    {
        BOOST_JSON_INVOKE_INNER( on_bool(ec, v) );
    }

    bool on_null( error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_null(ec) );
    }

#undef BOOST_JSON_INVOKE_INNER
};

// integral handler
template<class V,
typename std::enable_if<std::is_signed<V>::value, int>::type = 0>
bool integral_in_range( std::int64_t v )
{
    return v >= (std::numeric_limits<V>::min)() && v <= (std::numeric_limits<V>::max)();
}

template<class V,
typename std::enable_if<!std::is_signed<V>::value, int>::type = 0>
bool integral_in_range( std::int64_t v )
{
    return v >= 0 && static_cast<std::uint64_t>( v ) <= (std::numeric_limits<V>::max)();
}

template<class V>
bool integral_in_range( std::uint64_t v )
{
    return v <= static_cast<typename std::make_unsigned<V>::type>( (std::numeric_limits<V>::max)() );
}

template< class V, class P >
class converting_handler<integral_conversion_tag, V, P>
    : public scalar_handler<P, error::not_integer>
{
private:
    V* value_;

public:
    converting_handler( V* v, P* p )
        : converting_handler::scalar_handler(p)
        , value_(v)
    {}

    bool on_number_part( error_code&, string_view )
    {
        return true;
    }

    bool on_int64( error_code& ec, std::int64_t v, string_view )
    {
        if( !integral_in_range<V>( v ) )
        {
            BOOST_JSON_FAIL( ec, error::not_exact );
            return false;
        }

        *value_ = static_cast<V>( v );

        this->parent_->signal_value();
        return true;
    }

    bool on_uint64( error_code& ec, std::uint64_t v, string_view )
    {
        if( !integral_in_range<V>( v ) )
        {
            BOOST_JSON_FAIL( ec, error::not_exact );
            return false;
        }

        *value_ = static_cast<V>( v );

        this->parent_->signal_value();
        return true;
    }
};

// floating point handler
template< class V, class P>
class converting_handler<floating_point_conversion_tag, V, P>
    : public scalar_handler<P, error::not_double>
{
private:
    V* value_;

public:
    converting_handler( V* v, P* p )
        : converting_handler::scalar_handler(p)
        , value_(v)
    {}

    bool on_number_part( error_code&, string_view )
    {
        return true;
    }

    bool on_int64( error_code&, std::int64_t v, string_view )
    {
        *value_ = static_cast<V>( v );

        this->parent_->signal_value();
        return true;
    }

    bool on_uint64( error_code&, std::uint64_t v, string_view )
    {
        *value_ = static_cast<V>( v );

        this->parent_->signal_value();
        return true;
    }

    bool on_double( error_code&, double v, string_view )
    {
        *value_ = static_cast<V>( v );

        this->parent_->signal_value();
        return true;
    }
};

// string handler
template< class V, class P >
class converting_handler<string_like_conversion_tag, V, P>
    : public scalar_handler<P, error::not_string>
{
private:
    V* value_;

public:
    converting_handler( V* v, P* p )
        : converting_handler::scalar_handler(p)
        , value_(v)
    {}

    bool on_string_part( error_code&, string_view sv, std::size_t )
    {
        value_->append( sv.begin(), sv.end() );
        return true;
    }

    bool on_string( error_code&, string_view sv, std::size_t )
    {
        value_->append( sv.begin(), sv.end() );

        this->parent_->signal_value();
        return true;
    }
};

// bool handler
template< class V, class P >
class converting_handler<bool_conversion_tag, V, P>
    : public scalar_handler<P, error::not_bool>
{
private:
    V* value_;

public:
    converting_handler( V* v, P* p )
        : converting_handler::scalar_handler(p)
        , value_(v)
    {}

    bool on_bool( error_code&, bool v )
    {
        *value_ = v;

        this->parent_->signal_value();
        return true;
    }
};

// null handler
template< class V, class P >
class converting_handler<null_like_conversion_tag, V, P>
    : public scalar_handler<P, error::not_null>
{
private:
    V* value_;

public:
    converting_handler( V* v, P* p )
        : converting_handler::scalar_handler(p)
        , value_(v)
    {}

    bool on_null( error_code& )
    {
        *value_ = {};

        this->parent_->signal_value();
        return true;
    }
};

// described enum handler
template< class V, class P >
class converting_handler<described_enum_conversion_tag, V, P>
    : public scalar_handler<P, error::not_string>
{
#ifndef BOOST_DESCRIBE_CXX14

    static_assert(
        sizeof(V) == 0, "Enum support for parse_into requires C++14" );

#else

private:
    V* value_;
    std::string name_;

public:
    converting_handler( V* v, P* p )
        : converting_handler::scalar_handler(p)
        , value_(v)
    {}

    bool on_string_part( error_code&, string_view sv, std::size_t )
    {
        name_.append( sv.begin(), sv.end() );
        return true;
    }

    bool on_string( error_code& ec, string_view sv, std::size_t )
    {
        name_.append( sv.begin(), sv.end() );

        if( !describe::enum_from_string(name_.data(), *value_) )
        {
            BOOST_JSON_FAIL(ec, error::unknown_name);
            return false;
        }

        this->parent_->signal_value();
        return true;
    }

#endif // BOOST_DESCRIBE_CXX14
};

template< class V, class P >
class converting_handler<no_conversion_tag, V, P>
{
    static_assert( sizeof(V) == 0, "This type is not supported" );
};

// sequence handler
template< class V, class P >
class converting_handler<sequence_conversion_tag, V, P>
    : public composite_handler<
        converting_handler<sequence_conversion_tag, V, P>,
        detail::value_type<V>,
        P,
        error::not_array>
{
private:
    V* value_;

public:
    converting_handler( V* v, P* p )
        : converting_handler::composite_handler(p), value_(v)
    {}

    void signal_value()
    {
        value_->push_back( std::move(this->next_value_) );
        this->next_value_ = {};
    }

    bool on_array_begin( error_code& ec )
    {
        if( this->inner_active_ )
        {
            return this->inner_.on_array_begin( ec );
        }
        else
        {
            this->inner_active_ = true;
            return true;
        }
    }

    bool on_array_end( error_code& ec, std::size_t n )
    {
        if( this->inner_active_ )
        {
            return this->inner_.on_array_end( ec, n );
        }
        else
        {
            this->parent_->signal_end();
            return true;
        }
    }
};

// map handler
template< class V, class P >
class converting_handler<map_like_conversion_tag, V, P>
    : public composite_handler<
        converting_handler<map_like_conversion_tag, V, P>,
        detail::mapped_type<V>,
        P,
        error::not_object>
{
private:
    V* value_;
    std::string key_;

public:
    converting_handler( V* v, P* p )
        : converting_handler::composite_handler(p), value_(v)
    {}

    void signal_value()
    {
        value_->emplace( std::move(key_), std::move(this->next_value_) );

        key_ = {};
        this->next_value_ = {};

        this->inner_active_ = false;
    }

    bool on_object_begin( error_code& ec )
    {
        if( this->inner_active_ )
            return this->inner_.on_object_begin(ec);

        return true;
    }

    bool on_object_end( error_code& ec, std::size_t n )
    {
        if( this->inner_active_ )
            return this->inner_.on_object_end(ec, n);

        this->parent_->signal_value();
        return true;
    }

    bool on_array_end( error_code& ec, std::size_t n )
    {
        if( this->inner_active_ )
            return this->inner_.on_array_end(ec, n);

        this->parent_->signal_end();
        return true;
    }

    bool on_key_part( error_code& ec, string_view sv, std::size_t n )
    {
        if( this->inner_active_ )
            return this->inner_.on_key_part(ec, sv, n);

        key_.append( sv.data(), sv.size() );
        return true;
    }

    bool on_key( error_code& ec, string_view sv, std::size_t n )
    {
        if( this->inner_active_ )
            return this->inner_.on_key(ec, sv, n);

        key_.append( sv.data(), sv.size() );

        this->inner_active_ = true;
        return true;
    }
};

// tuple handler
template<std::size_t I, class T>
struct handler_tuple_element
{
    template< class... Args >
    handler_tuple_element( Args&& ... args )
        : t_( static_cast<Args&&>(args)... )
    {}

    T t_;
};

template<std::size_t I, class T>
T&
get( handler_tuple_element<I, T>& e )
{
    return e.t_;
}

template<
    class P,
    class LV,
    class S = mp11::make_index_sequence<mp11::mp_size<LV>::value> >
struct handler_tuple;

template< class P, template<class...> class L, class... V, std::size_t... I >
struct handler_tuple< P, L<V...>, mp11::index_sequence<I...> >
    : handler_tuple_element< I, get_handler<V, P> >
    ...
{
    handler_tuple( handler_tuple const& ) = delete;
    handler_tuple& operator=( handler_tuple const& ) = delete;

    template< class Access, class T >
    handler_tuple( Access access, T* pv, P* pp )
        : handler_tuple_element< I, get_handler<V, P> >(
            access( pv, mp11::mp_int<I>() ), pp )
        ...
    { }
};

#if defined(BOOST_MSVC) && BOOST_MSVC < 1910

template< class T >
struct tuple_element_list_impl
{
    template< class I >
    using tuple_element_helper = tuple_element_t<I::value, T>;

    using type = mp11::mp_transform<
        tuple_element_helper,
        mp11::mp_iota< std::tuple_size<T> > >;
};
template< class T >
using tuple_element_list = typename tuple_element_list_impl<T>::type;

#else

template< class I, class T >
using tuple_element_helper = tuple_element_t<I::value, T>;
template< class T >
using tuple_element_list = mp11::mp_transform_q<
    mp11::mp_bind_back< tuple_element_helper, T>,
    mp11::mp_iota< std::tuple_size<T> > >;

#endif

struct tuple_accessor
{
    template< class T, class I >
    auto operator()( T* t, I ) const -> tuple_element_t<I::value, T>*
    {
        using std::get;
        return &get<I::value>(*t);
    }
};

template< class T, class P >
class converting_handler<tuple_conversion_tag, T, P>
{
#if BOOST_CXX_VERSION < 201400L || ( defined(BOOST_MSVC) && BOOST_MSVC < 1910 )

    static_assert(
        sizeof(T) == 0, "Tuple support for parse_into requires C++14 or MSVC 2017 or later" );

#else

private:
    T* value_;
    P* parent_;

    handler_tuple< converting_handler, tuple_element_list<T> > handlers_;
    int inner_active_ = -1;

public:
    converting_handler( converting_handler const& ) = delete;
    converting_handler& operator=( converting_handler const& ) = delete;

    converting_handler( T* v, P* p )
        : value_(v) , parent_(p) , handlers_(tuple_accessor(), v, this)
    {}

    void signal_value()
    {
        ++inner_active_;
    }

    void signal_end()
    {
        inner_active_ = -1;
        parent_->signal_value();
    }

#define BOOST_JSON_INVOKE_INNER(fn) \
    if( inner_active_ < 0 ) \
    { \
        BOOST_JSON_FAIL( ec, error::not_array ); \
        return false; \
    } \
    constexpr int N = std::tuple_size<T>::value; \
    if( inner_active_ >= N ) \
    { \
        BOOST_JSON_FAIL( ec, error::size_mismatch ); \
        return false; \
    } \
    return mp11::mp_with_index<N>( inner_active_, [&](auto I){ \
        return get<I.value>( handlers_ ).fn; \
    });

    bool on_object_begin( error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_object_begin(ec) );
    }

    bool on_object_end( error_code& ec, std::size_t n )
    {
        BOOST_JSON_INVOKE_INNER( on_object_end(ec, n) );
    }

    bool on_array_begin( error_code& ec )
    {
        if( inner_active_ < 0 )
        {
            inner_active_ = 0;
            return true;
        }

        constexpr int N = std::tuple_size<T>::value;

        if( inner_active_ >= N )
        {
            inner_active_ = 0;
            return true;
        }

        return mp11::mp_with_index<N>( inner_active_, [&](auto I){
            return get<I.value>( handlers_ ).on_array_begin(ec);
        });
    }

    bool on_array_end( error_code& ec, std::size_t n )
    {
        if( inner_active_ < 0 )
        {
            parent_->signal_end();
            return true;
        }

        constexpr int N = std::tuple_size<T>::value;

        if( inner_active_ >= N )
        {
            parent_->signal_value();

            inner_active_ = -1;
            return true;
        }

        return mp11::mp_with_index<N>( inner_active_, [&](auto I){
            return get<I.value>( handlers_ ).on_array_end( ec, n );
        });
    }

    bool on_key_part( error_code& ec, string_view sv, std::size_t n )
    {
        BOOST_JSON_INVOKE_INNER( on_key_part(ec, sv, n) );
    }

    bool on_key( error_code& ec, string_view sv, std::size_t n )
    {
        BOOST_JSON_INVOKE_INNER( on_key(ec, sv, n) );
    }

    bool on_string_part( error_code& ec, string_view sv, std::size_t n )
    {
        BOOST_JSON_INVOKE_INNER( on_string_part(ec, sv, n) );
    }

    bool on_string( error_code& ec, string_view sv, std::size_t n )
    {
        BOOST_JSON_INVOKE_INNER( on_string(ec, sv, n) );
    }

    bool on_number_part( error_code& ec, string_view sv )
    {
        BOOST_JSON_INVOKE_INNER( on_number_part(ec, sv) );
    }

    bool on_int64( error_code& ec, std::int64_t v, string_view sv )
    {
        BOOST_JSON_INVOKE_INNER( on_int64(ec, v, sv) );
    }

    bool on_uint64( error_code& ec, std::uint64_t v, string_view sv )
    {
        BOOST_JSON_INVOKE_INNER( on_uint64(ec, v, sv) );
    }

    bool on_double( error_code& ec, double v, string_view sv )
    {
        BOOST_JSON_INVOKE_INNER( on_double(ec, v, sv) );
    }

    bool on_bool( error_code& ec, bool v )
    {
        BOOST_JSON_INVOKE_INNER( on_bool(ec, v) );
    }

    bool on_null( error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_null(ec) );
    }

#undef BOOST_JSON_INVOKE_INNER

#endif
};

// described struct handler
#if defined(BOOST_MSVC) && BOOST_MSVC < 1910

template< class T >
struct struct_element_list_impl
{
    template< class D >
    using helper = described_member_t<T, D>;

    using type = mp11::mp_transform<
        helper,
        describe::describe_members<T, describe::mod_public> >;
};
template< class T >
using struct_element_list = typename struct_element_list_impl<T>::type;

#else

template< class T >
using struct_element_list = mp11::mp_transform_q<
    mp11::mp_bind_front< described_member_t, T >,
    describe::describe_members<T, describe::mod_public> >;

#endif

struct struct_accessor
{
    template< class T, class I >
    auto operator()( T* t, I ) const
        -> described_member_t<T,
             mp11::mp_at<
                 describe::describe_members<T, describe::mod_public>, I> >*
    {
        using Ds = describe::describe_members<T, describe::mod_public>;
        using D = mp11::mp_at<Ds, I>;
        return &(t->*D::pointer);
    }
};

template<class V, class P>
class converting_handler<described_class_conversion_tag, V, P>
{
#if BOOST_CXX_VERSION < 201400L || ( defined(BOOST_MSVC) && BOOST_MSVC < 1910 )

    static_assert(
        sizeof(V) == 0, "Struct support for parse_into requires C++14 or MSVC 2017 or later" );

#else

private:
    V* value_;
    P* parent_;

    std::string key_;

    using Dm = describe::describe_members<V, describe::mod_public>;

    handler_tuple< converting_handler, struct_element_list<V> > handlers_;
    int inner_active_ = -1;

public:
    converting_handler( converting_handler const& ) = delete;
    converting_handler& operator=( converting_handler const& ) = delete;

    converting_handler( V* v, P* p )
        : value_(v), parent_(p), handlers_(struct_accessor(), v, this)
    {}

    void signal_value()
    {
        key_ = {};
        inner_active_ = -1;
    }

    void signal_end()
    {
        key_ = {};
        inner_active_ = -1;

        parent_->signal_value();
    }

#define BOOST_JSON_INVOKE_INNER(fn) \
    if( inner_active_ < 0 ) \
    { \
        BOOST_JSON_FAIL( ec, error::not_object ); \
        return false; \
    } \
    return mp11::mp_with_index<mp11::mp_size<Dm>>( inner_active_, [&](auto I) { \
        return get<I.value>( handlers_ ).fn; \
    });

    bool on_object_begin( error_code& ec )
    {
        if( inner_active_ < 0 )
            return true;

        BOOST_JSON_INVOKE_INNER( on_object_begin(ec) );
    }

    bool on_object_end( error_code& ec, std::size_t n )
    {
        if( inner_active_ < 0 )
        {
            parent_->signal_value();
            return true;
        }

        BOOST_JSON_INVOKE_INNER( on_object_end(ec, n) );
    }

    bool on_array_begin( error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_array_begin(ec) );
    }

    bool on_array_end( error_code& ec, std::size_t n )
    {
        if( inner_active_ < 0 )
        {
            parent_->signal_end();
            return true;
        }

        BOOST_JSON_INVOKE_INNER( on_array_end(ec, n) );
    }

    bool on_key_part( error_code& ec, string_view sv, std::size_t n )
    {
        if( inner_active_ < 0 )
        {
            key_.append( sv.data(), sv.size() );
            return true;
        }

        BOOST_JSON_INVOKE_INNER( on_key_part(ec, sv, n) );
    }

    bool on_key( error_code& ec, string_view sv, std::size_t n )
    {
        if( inner_active_ >= 0 )
        {
            BOOST_JSON_INVOKE_INNER( on_key(ec, sv, n) );
        }

        key_.append( sv.data(), sv.size() );

        int i = 0;

        mp11::mp_for_each<Dm>([&](auto D) {
            if( key_ == D.name )
                inner_active_ = i;
            ++i;
        });

        if( inner_active_ < 0 )
        {
            BOOST_JSON_FAIL(ec, error::unknown_name);
            return false;
        }

        return true;
    }

    bool on_string_part( error_code& ec, string_view sv, std::size_t n )
    {
        BOOST_JSON_INVOKE_INNER( on_string_part(ec, sv, n) );
    }

    bool on_string( error_code& ec, string_view sv, std::size_t n )
    {
        BOOST_JSON_INVOKE_INNER( on_string(ec, sv, n) );
    }

    bool on_number_part( error_code& ec, string_view sv )
    {
        BOOST_JSON_INVOKE_INNER( on_number_part(ec, sv) );
    }

    bool on_int64( error_code& ec, std::int64_t v, string_view sv )
    {
        BOOST_JSON_INVOKE_INNER( on_int64(ec, v, sv) );
    }

    bool on_uint64( error_code& ec, std::uint64_t v, string_view sv )
    {
        BOOST_JSON_INVOKE_INNER( on_uint64(ec, v, sv) );
    }

    bool on_double( error_code& ec, double v, string_view sv )
    {
        BOOST_JSON_INVOKE_INNER( on_double(ec, v, sv) );
    }

    bool on_bool( error_code& ec, bool v )
    {
        BOOST_JSON_INVOKE_INNER( on_bool(ec, v) );
    }

    bool on_null( error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_null(ec) );
    }

#undef BOOST_JSON_INVOKE_INNER

#endif
};

// optional handler
template<class V, class P>
class converting_handler<optional_conversion_tag, V, P>
{
private:
    using inner_type = value_result_type<V>;
    using inner_handler_type = get_handler<inner_type, converting_handler>;

    V* value_;
    P* parent_;

    inner_type inner_value_ = {};
    inner_handler_type inner_;
    bool inner_active_ = false;

public:
    converting_handler( converting_handler const& ) = delete;
    converting_handler& operator=( converting_handler const& ) = delete;

    converting_handler( V* v, P* p )
        : value_(v), parent_(p), inner_(&inner_value_, this)
    {}

    void signal_value()
    {
        *value_ = std::move(inner_value_);

        inner_active_ = false;
        parent_->signal_value();
    }

    void signal_end()
    {
        parent_->signal_end();
    }

#define BOOST_JSON_INVOKE_INNER(fn) \
    if( !inner_active_ ) \
        inner_active_ = true; \
    return inner_.fn;

    bool on_object_begin( error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_object_begin(ec) );
    }

    bool on_object_end( error_code& ec, std::size_t n )
    {
        BOOST_JSON_INVOKE_INNER( on_object_end(ec, n) );
    }

    bool on_array_begin( error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_array_begin(ec) );
    }

    bool on_array_end( error_code& ec, std::size_t n )
    {
        if( !inner_active_ )
        {
            signal_end();
            return true;
        }

        BOOST_JSON_INVOKE_INNER( on_array_end(ec, n) );
    }

    bool on_key_part( error_code& ec, string_view sv, std::size_t n )
    {
        BOOST_JSON_INVOKE_INNER( on_key_part(ec, sv, n) );
    }

    bool on_key( error_code& ec, string_view sv, std::size_t n )
    {
        BOOST_JSON_INVOKE_INNER( on_key(ec, sv, n) );
    }

    bool on_string_part( error_code& ec, string_view sv, std::size_t n )
    {
        BOOST_JSON_INVOKE_INNER( on_string_part(ec, sv, n) );
    }

    bool on_string( error_code& ec, string_view sv, std::size_t n )
    {
        BOOST_JSON_INVOKE_INNER( on_string(ec, sv, n) );
    }

    bool on_number_part( error_code& ec, string_view sv )
    {
        BOOST_JSON_INVOKE_INNER( on_number_part(ec, sv) );
    }

    bool on_int64( error_code& ec, std::int64_t v, string_view sv )
    {
        BOOST_JSON_INVOKE_INNER( on_int64(ec, v, sv) );
    }

    bool on_uint64( error_code& ec, std::uint64_t v, string_view sv )
    {
        BOOST_JSON_INVOKE_INNER( on_uint64(ec, v, sv) );
    }

    bool on_double( error_code& ec, double v, string_view sv )
    {
        BOOST_JSON_INVOKE_INNER( on_double(ec, v, sv) );
    }

    bool on_bool( error_code& ec, bool v )
    {
        BOOST_JSON_INVOKE_INNER( on_bool(ec, v) );
    }

    bool on_null( error_code& ec )
    {
        if( !inner_active_ )
        {
            *value_ = {};

            this->parent_->signal_value();
            return true;
        }
        else
        {
            return inner_.on_null(ec);
        }
    }

#undef BOOST_JSON_INVOKE_INNER
};

// into_handler
template< class V >
class into_handler
{
private:

    using inner_handler_type = get_handler<V, into_handler>;

    inner_handler_type inner_;
    bool inner_active_ = true;

public:

    into_handler( into_handler const& ) = delete;
    into_handler& operator=( into_handler const& ) = delete;

public:

    static constexpr std::size_t max_object_size = object::max_size();
    static constexpr std::size_t max_array_size = array::max_size();
    static constexpr std::size_t max_key_size = string::max_size();
    static constexpr std::size_t max_string_size = string::max_size();

public:

    explicit into_handler( V* v ): inner_( v, this )
    {
    }

    void signal_value()
    {
    }

    void signal_end()
    {
    }

    bool on_document_begin( error_code& )
    {
        return true;
    }

    bool on_document_end( error_code& )
    {
        inner_active_ = false;
        return true;
    }

#define BOOST_JSON_INVOKE_INNER(f) \
    if( !inner_active_ ) \
    { \
        BOOST_JSON_FAIL( ec, error::extra_data ); \
        return false; \
    } \
    else \
        return inner_.f

    bool on_object_begin( error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_object_begin(ec) );
    }

    bool on_object_end( std::size_t n, error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_object_end(ec, n) );
    }

    bool on_array_begin( error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_array_begin(ec) );
    }

    bool on_array_end( std::size_t n, error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_array_end(ec, n) );
    }

    bool on_key_part( string_view sv, std::size_t n, error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_key_part(ec, sv, n) );
    }

    bool on_key( string_view sv, std::size_t n, error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_key(ec, sv, n) );
    }

    bool on_string_part( string_view sv, std::size_t n, error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_string_part(ec, sv, n) );
    }

    bool on_string( string_view sv, std::size_t n, error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_string(ec, sv, n) );
    }

    bool on_number_part( string_view sv, error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_number_part(ec, sv) );
    }

    bool on_int64( std::int64_t v, string_view sv, error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_int64(ec, v, sv) );
    }

    bool on_uint64( std::uint64_t v, string_view sv, error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_uint64(ec, v, sv) );
    }

    bool on_double( double v, string_view sv, error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_double(ec, v, sv) );
    }

    bool on_bool( bool v, error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_bool(ec, v) );
    }

    bool on_null( error_code& ec )
    {
        BOOST_JSON_INVOKE_INNER( on_null(ec) );
    }

    bool on_comment_part(string_view, error_code&)
    {
        return true;
    }

    bool on_comment(string_view, error_code&)
    {
        return true;
    }

#undef BOOST_JSON_INVOKE_INNER
};

} // namespace detail
} // namespace boost
} // namespace json

#endif