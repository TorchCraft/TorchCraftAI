/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "module.h"
#include "registry.h"

using namespace cherrypi;

namespace {

// Zero ctor arguments
struct Base0 {
  Base0() {}
  virtual ~Base0() {}
  virtual std::string id() {
    return "base0";
  }
};

struct Derived0A : public Base0 {
  virtual std::string id() override {
    return "derived0A";
  }
};
REGISTER_SUBCLASS_0(Base0, Derived0A);
// REGISTER_SUBCLASS_0(Base0, Derived0A, int); // Does not compile because
// unmatched constructor

struct Derived0B : public Base0 {
  virtual std::string id() override {
    return "derived0B";
  }
};
REGISTER_SUBCLASS_0(Base0, Derived0B);
namespace {
REGISTER_SUBCLASS_0(
    Base0,
    Derived0B); // Double reg only compiles in separate context but is ok
}

struct DerivedNotReg : public Base0 {
  virtual std::string id() override {
    return "nonderived";
  }
};
// No registration

struct Derived0AA : public Derived0A {
  virtual std::string id() override {
    return "derived0AA";
  }
};
REGISTER_SUBCLASS_0(Base0, Derived0AA);

struct DerivedFrom0A : public Derived0A {
  virtual std::string id() override {
    return "derivedfrom0A";
  }
};
REGISTER_SUBCLASS_0(Base0, DerivedFrom0A);
REGISTER_SUBCLASS_0(Derived0A, DerivedFrom0A); // Compiles, different base

// One ctor argument
struct Base1 {
  Base1(std::string const& name) : name(name) {}
  virtual ~Base1() = default;
  virtual std::string id() {
    return "base1";
  }
  std::string name;
};

struct Derived1A : public Base1 {
  Derived1A(std::string const& name) : Base1(name) {}
  Derived1A(int) : Base1("no name") {}
  virtual std::string id() override {
    return "derived1A";
  }
};
REGISTER_SUBCLASS_1(Base1, Derived1A, std::string const&);
// REGISTER_SUBCLASS_0(Base1, Derived1A, int); Does not compile, can't register
// type/base pair twice
namespace {
REGISTER_SUBCLASS_1(Base1, Derived1A, int); // Different scope works...
}

// Module tests
class DummyBaseModule : public Module {
  virtual void step(State*) override {}
};

class DummyModule : public DummyBaseModule {};
REGISTER_SUBCLASS_0(Module, DummyModule);

} // namespace

CASE("registry/basic") {
  EXPECT(SubclassRegistry<Base0>::subclasses().size() == 4u);
  EXPECT(SubclassRegistry<Base0>::record("foobar") == nullptr);
  EXPECT(SubclassRegistry<Base0>::record("Derived0A") != nullptr);
  EXPECT(
      SubclassRegistry<Base0>::record("Derived0A")->type == typeid(Derived0A));
  EXPECT(
      SubclassRegistry<Base0>::record("derived0a")->type == typeid(Derived0A));
  EXPECT(
      SubclassRegistry<Base0>::record("DERIVED0a")->type == typeid(Derived0A));
  EXPECT(SubclassRegistry<Base0>::record("Derived0A")->name == "Derived0A");
  EXPECT(
      SubclassRegistry<Base0>::record("Derived0A")->ctor()->id() ==
      "derived0A");
  EXPECT(SubclassRegistry<Base0>::create("Derived0A")->id() == "derived0A");
  EXPECT(SubclassRegistry<Base0>::name(typeid(Derived0A)) == "Derived0A");
  EXPECT(SubclassRegistry<Base0>::name<Derived0A>() == "Derived0A");
  EXPECT(SubclassRegistry<Base0>::name<Derived0B>() == "Derived0B");
  EXPECT(SubclassRegistry<Base0>::record("Base0") == nullptr);
  EXPECT(SubclassRegistry<Base0>::name<Base0>() == "");
  EXPECT(SubclassRegistry<Base0>::record("DerivedFrom0A") != nullptr);
  EXPECT(SubclassRegistry<Derived0A>::record("DerivedFrom0A") != nullptr);
  EXPECT(SubclassRegistry<Base0>::record("DerivedNotReg") == nullptr);
  EXPECT(SubclassRegistry<Base0>::name<DerivedNotReg>() == "");

  EXPECT(SubclassRegistry<Base1>::subclasses().size() == 0u);
  auto sz = SubclassRegistry<Base1, std::string const&>::subclasses().size();
  EXPECT(sz == 1u); // poor lest can't parse the expression above
  sz = SubclassRegistry<Base1, int>::subclasses().size();
  EXPECT(sz == 1u);
  auto* record1 =
      SubclassRegistry<Base1, std::string const&>::record("Derived1A");
  EXPECT(record1 != nullptr);
  auto* record2 = SubclassRegistry<Base1, int>::record("Derived1A");
  EXPECT(record2 != nullptr);
  auto* record3 = SubclassRegistry<Base1, bool>::record("Derived1A");
  EXPECT(record3 == nullptr);
  auto inst1 =
      SubclassRegistry<Base1, std::string const&>::create("Derived1A", "test");
  EXPECT(inst1->id() == "derived1A");
  EXPECT(inst1->name == "test");
  auto inst2 = SubclassRegistry<Base1, int>::create("Derived1A", 0);
  EXPECT(inst2->id() == "derived1A");
  EXPECT(inst2->name == "no name");
  //  auto inst3 = SubclassRegistry<Base1, bool>::create("Derived1A"); // does
  //  not compile
  auto inst3 = SubclassRegistry<Base1, bool>::create("Derived1A", true);
  EXPECT(inst3 == nullptr);
}

CASE("registry/modules/access") {
  // Note that we don't include TopModule's header here
  EXPECT(Module::make("top") != nullptr);
  EXPECT(Module::make("top")->name() == "TopModule");
}

CASE("registry/modules/template") {
  // Obvious tests, but they will stop compiling if something bad happens
  auto db = Module::make<DummyBaseModule>();
  EXPECT(std::dynamic_pointer_cast<Module>(db) != nullptr);
  EXPECT(std::dynamic_pointer_cast<DummyBaseModule>(db) != nullptr);
  EXPECT(std::dynamic_pointer_cast<DummyModule>(db) == nullptr);

  auto d = Module::make<DummyModule>();
  EXPECT(std::dynamic_pointer_cast<Module>(d) != nullptr);
  EXPECT(std::dynamic_pointer_cast<DummyBaseModule>(d) != nullptr);
  EXPECT(std::dynamic_pointer_cast<DummyModule>(d) != nullptr);
}

CASE("registry/modules/name") {
  // Not all modules are registered
  EXPECT(Module::make("module") == nullptr);
  EXPECT(Module::make("lambda") == nullptr);
  EXPECT(Module::make("dummybase") == nullptr);
  EXPECT(Module::make("dummybasemodule") == nullptr);

  // Works well for modules with a registered constructor
  EXPECT(Module::make("dummy") != nullptr);
  EXPECT(Module::make("dUmMY") != nullptr);
  EXPECT(Module::make("dummymodule") != nullptr);
  EXPECT(Module::make("DummyModule") != nullptr);
}
