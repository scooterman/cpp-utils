#pragma once

#include <boost/interprocess/detail/atomic.hpp>
#include <boost/thread.hpp>
#include <boost/function.hpp>

#include <iostream>

#include <semaphore.h>

#include <tbb/atomic.h>
#include <tbb/task.h>

struct scope_waiter
{
    scope_waiter(sem_t& futex)
            : mr_sem(futex)
    {
    }

    scope_waiter(const scope_waiter& other)
            : mr_sem(other.mr_sem)
    {
    }

    ~scope_waiter()
    {
        sem_post(&mr_sem);
    }

protected:
    sem_t& mr_sem;
};

struct unsynched_t
{
};

struct synched_t
{
    synched_t()
    {
        m_count = 0;
        sem_init(&m_sem, 0 , 0);
    }

    scope_waiter register_lock()
    {
        m_count++;
        return scope_waiter(m_sem);
    }
    
    void wait_for_all()
    {
        for (unsigned int  i = 0; i < m_count; ++i)
        {
            sem_wait(&m_sem);
        }
        m_count = 0;
    }

    ~synched_t()
    {
      wait_for_all();
    }

protected:
    sem_t m_sem;
    tbb::atomic<unsigned int> m_count;
};

template < typename function_t>
class contended_caller : public tbb::task
{
public:
    contended_caller(synched_t& sb, function_t& func)
            : m_sw(sb.register_lock()) , m_func(func)
    {
    }

    tbb::task* execute()
    {
        m_func();

        return NULL;
    }

    scope_waiter m_sw;
    function_t m_func;
};

template <typename function_t>
class simple_caller : public tbb::task
{
public:
    simple_caller( function_t& func)
            :  m_func(func)
    {
    }

    tbb::task* execute()
    {
        m_func();
        return NULL;
    }

    function_t m_func;
};


template < uint N >
struct apply_obj_func
{
    template < typename function_t, typename... ArgsT, typename... Args >
    static void applyTuple( function_t f,
                            const std::tuple<ArgsT...>& t,
                            Args... args )
    {
        apply_obj_func<N-1>::applyTuple( f, t, std::get<N-1>( t ), args... );
    }
};

template <>
struct apply_obj_func<0>
{
    template < typename function_t, typename... ArgsT, typename... Args >
    static void applyTuple( function_t f,
                            const std::tuple<ArgsT...>& /* t */,
                            Args... args )
    {
        f( args... );
    }
};


struct async
{
    template <typename synched_t, typename function_t>
    async (synched_t& sb, function_t func)
    {
        contended_caller<function_t>& cc = * new (tbb::task::allocate_root()) contended_caller<function_t>(sb, func);
        tbb::task::spawn(cc);
    }

    template < typename function_t>
    async (function_t func)
    {
        simple_caller<function_t>& sc = * new (tbb::task::allocate_root()) simple_caller<function_t>(func);
        tbb::task::spawn(sc);
    }
    
    
    template <typename synched_t, typename function_t, typename... parameters>
    async(synched_t& sb, function_t f, parameters... params)
    {
        class forwarded_callable : public tbb::task
        {
        public:
            forwarded_callable(synched_t& sb, function_t f, parameters... p)
                    : m_function(f), m_parameters(p...), m_sw(sb.register_lock())
            {
            }

            tbb::task* execute()
            {
                apply_obj_func<sizeof... (parameters) >::applyTuple(m_function, m_parameters);
                return NULL;
            }

            std::tuple<parameters...> m_parameters;
            function_t m_function;
            scope_waiter m_sw;
        };

        forwarded_callable& sc = * new (tbb::task::allocate_root()) forwarded_callable(sb, f, params...);
        tbb::task::spawn(sc);
    }

    template <typename function_t, typename... parameters>
    async(function_t f, parameters... params)
    {
        class forwarded_callable : public tbb::task
        {
        public:
            forwarded_callable(function_t f, parameters... p)
                    : m_function(f), m_parameters(p...)
            {
            }

            tbb::task* execute()
            {
                apply_obj_func<sizeof... (parameters) >::applyTuple(m_function, m_parameters);
                return NULL;
            }

            std::tuple<parameters...> m_parameters;
            function_t m_function;
        };

        forwarded_callable& sc = * new (tbb::task::allocate_root()) forwarded_callable(f, params...);
        tbb::task::spawn(sc);
    }
};