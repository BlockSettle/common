#include "ValidityFlag.h"


ValidityHandle::ValidityHandle(ValidityFlag *parent)
{
   setParent(parent);
}

ValidityHandle::~ValidityHandle()
{
   setParent(nullptr);
}

ValidityHandle::ValidityHandle(const ValidityHandle &other)
{
   setParent(other.parent_);
}

void ValidityHandle::setParent(ValidityFlag *parent)
{
   if (parent_) {
      parent_->handles_.erase(this);
   }

   parent_ = parent;

   if (parent_) {
      parent_->handles_.insert(this);
   }
}

ValidityHandle::operator bool() const
{
   return (parent_ != nullptr);
}


ValidityFlag::ValidityFlag() = default;

ValidityFlag::~ValidityFlag()
{
   for (auto &child : handles_) {
      child->parent_ = nullptr;
   }
   handles_.clear();
}

ValidityHandle ValidityFlag::handle()
{
   return ValidityHandle(this);
}
