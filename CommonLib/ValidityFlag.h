#ifndef VALIDITY_FLAG_H
#define VALIDITY_FLAG_H

// Use to detect in when some class was destroyed (implementation is not thread-safe)
//
// Example:
//   class Test
//   {
//      Test()
//      {
//         auto safeCallback = [this, handle = validityFlag_.handle()] {
//            if (!handle) {
//               return;
//            }
//            // proceed..
//         };
//      }
//   private:
//      ValidityFlag validityFlag_;
//   };

#include <unordered_set>

class ValidityFlag;

class ValidityHandle
{
public:
   ValidityHandle(ValidityFlag *parent);
   ~ValidityHandle();

   ValidityHandle(const ValidityHandle& other);
   ValidityHandle& operator = (const ValidityHandle& other);

   operator bool() const;

private:
   friend class ValidityFlag;

   void setParent(ValidityFlag *parent);

   ValidityFlag *parent_{};

};

class ValidityFlag
{
public:
   ValidityFlag();
   ~ValidityFlag();

   ValidityFlag(const ValidityFlag&) = delete;
   ValidityFlag& operator = (const ValidityFlag&) = delete;

   ValidityHandle handle();

private:
   friend class ValidityHandle;

   std::unordered_set<ValidityHandle*> handles_;

};

#endif
