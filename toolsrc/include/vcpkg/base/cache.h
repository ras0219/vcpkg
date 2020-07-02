#pragma once

#include <map>
#include <mutex>

namespace vcpkg
{
    template<class Key, class Value>
    struct Cache
    {
        template<class F>
        Value const& get_lazy(const Key& k, const F& f) const
        {
            std::lock_guard<std::mutex> lk(m);
            auto it = m_cache.find(k);
            if (it != m_cache.end()) return it->second;
            return m_cache.emplace(k, f()).first->second;
        }

    private:
        mutable std::map<Key, Value> m_cache;
        mutable std::mutex m;
    };
}
