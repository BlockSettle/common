#ifndef __SCOPED_FLAG_H__
#define __SCOPED_FLAG_H__

#include <type_traits>

template<typename BooleanType>
class ScopedFlag
{
public:
   ScopedFlag() = delete;
   explicit ScopedFlag(BooleanType& flag)
    : flag_{flag}
   {
      flag_ = true;
   }

   ~ScopedFlag() noexcept
   {
      flag_ = false;
   }

   ScopedFlag(const ScopedFlag&) = delete;
   ScopedFlag& operator = (const ScopedFlag&) = delete;

   ScopedFlag(ScopedFlag&&) = delete;
   ScopedFlag& operator = (ScopedFlag&&) = delete;
private:
   BooleanType& flag_;
};

#endif // __SCOPED_FLAG_H__
