#include <string>
#include <sstream>
#include <iostream>
#include <vector>
#include <map>
#include <stdexcept>
#include <type_traits>
#include <memory>
#include <boost/lexical_cast.hpp>

namespace reflection {

// Move these helper traits before TypeTraits
template<typename T, typename = void>
struct is_lexical_castable {
private:
    template<typename U>
    static auto test(int) -> decltype(
        boost::lexical_cast<U>(std::string()),
        boost::lexical_cast<std::string>(std::declval<U>()),
        std::true_type()
    );

    template<typename>
    static auto test(...) -> std::false_type;

public:
    static constexpr bool value = 
        !std::is_same_v<T, void> && 
        std::is_default_constructible_v<T> &&
        decltype(test<T>(0))::value;
};

template<typename T>
inline constexpr bool is_lexical_castable_v = is_lexical_castable<T>::value;

// Now TypeTraits can use is_lexical_castable_v
template<typename T, typename = void>
struct TypeTraits {
    static T fromString(const std::string& str) {
        static_assert(is_lexical_castable_v<T>, 
            "Type must either support lexical_cast or have custom TypeTraits specialization");
        return boost::lexical_cast<T>(str);
    }
    
    static std::string toString(const T& val) {
        static_assert(is_lexical_castable_v<T>, 
            "Type must either support lexical_cast or have custom TypeTraits specialization");
        return boost::lexical_cast<std::string>(val);
    }
};

// Forward declarations
template<typename T>
struct Reflector;

// Base class for member info
class MemberInfoBase {
public:
    virtual ~MemberInfoBase() = default;
    virtual std::string getValue() const = 0;
    virtual bool setValue(const std::string& value) = 0;
};

// Templated member info implementation
template<typename T>
class MemberInfo : public MemberInfoBase {
    T* member;
public:
    explicit MemberInfo(T* ptr) : member(ptr) {}

    std::string getValue() const override {
        return TypeTraits<T>::toString(*member);
    }

    bool setValue(const std::string& value) override {
        try {
            *member = TypeTraits<T>::fromString(value);
            return true;
        } catch (...) {
            return false;
        }
    }
};

// Helper macros for member collection
#define REFLECT_CONCAT_(x,y) x##y
#define REFLECT_CONCAT(x,y) REFLECT_CONCAT_(x,y)
#define REFLECT_STRINGIFY(x) #x
#define REFLECT_COUNTER __COUNTER__

// Object registry
template<typename T>
class ObjectRegistry {
private:
    static std::map<std::string, T*> objects;

public:
    static void registerObject(const std::string& id, T* obj) {
        objects[id] = obj;
    }

    static void unregisterObject(const std::string& id) {
        objects.erase(id);
    }

    static T* getObject(const std::string& id) {
        auto it = objects.find(id);
        return it != objects.end() ? it->second : nullptr;
    }
};

// member_path -> T*
template<typename T>
std::map<std::string, T*> ObjectRegistry<T>::objects;

// Add forward declaration at the top of the namespace, before Reflectable class
template<typename T>
struct Reflector;

// Base class for reflectable objects - simplified version
template<typename Derived>
class Reflectable {
    friend class Reflector<Derived>;

private:
    std::string _object_id;

protected:
    // Constructor requires explicit ID
    explicit Reflectable(std::string id) {
        static_assert(std::is_base_of_v<Reflectable<Derived>, Derived>,
            "Derived class must inherit from Reflectable<Derived>");
        _register_self(std::move(id));
    }

    void _register_self(std::string id) {
        if (id.empty()) {
            throw std::invalid_argument("Object ID cannot be empty");
        }
        _object_id = std::move(id);
        ObjectRegistry<Derived>::registerObject(_object_id, static_cast<Derived*>(this));
    }

public:
    const std::string& getObjectId() const { return _object_id; }

    void registerAs(const std::string& id) {
        if (id.empty()) {
            throw std::invalid_argument("Object ID cannot be empty");
        }
        if (!_object_id.empty()) {
            ObjectRegistry<Derived>::unregisterObject(_object_id);
        }
        _register_self(id);
    }

    virtual ~Reflectable() {
        if (!_object_id.empty()) {
            ObjectRegistry<Derived>::unregisterObject(_object_id);
        }
    }

    template<size_t N>
    static constexpr auto _get_reflection_data(std::integral_constant<size_t, N>) {
        if constexpr (N > 0) {
            using MemberType = decltype(Derived::REFLECT_CONCAT(_reflect_member_, N-1));
            return std::tuple_cat(
                std::make_tuple(MemberType{}),
                _get_reflection_data(std::integral_constant<size_t, N-1>{})
            );
        } else {
            return std::make_tuple();
        }
    }

    static constexpr auto _reflect_members() {
        constexpr size_t count = REFLECT_COUNTER;
        return _get_reflection_data(std::integral_constant<size_t, count>{});
    }
};

// Helper macros for member reflection
#define REFLECT_MEMBER(Type, Name, DefaultValue)                                \
    Type Name = DefaultValue;                                                   \
    struct REFLECT_CONCAT(member_info_, Name) {                                \
        static constexpr const char* name = REFLECT_STRINGIFY(Name);           \
        using type = Type;                                                     \
        template<typename T>                                                   \
        static auto pointer() {                                                \
            return &T::Name;                                                  \
        }                                                                      \
    };                                                                         \
    static constexpr auto REFLECT_CONCAT(_reflect_member_, REFLECT_COUNTER) = \
        REFLECT_CONCAT(member_info_, Name){};

// Example classes
struct Record : public Reflectable<Record> {
    REFLECT_MEMBER(int, a, 0)
    REFLECT_MEMBER(std::string, b, "")

    Record() : Reflectable<Record>("default") {}
    explicit Record(std::string id) : Reflectable<Record>(std::move(id)) {}
};

template<>
struct TypeTraits<Record> {
    static Record fromString(const std::string& str) {
        Record r("default");
        auto pos = str.find(',');
        if (pos != std::string::npos) {
            r.a = boost::lexical_cast<int>(str.substr(0, pos));
            r.b = str.substr(pos + 1);
        }
        return r;
    }
    
    static std::string toString(const Record& val) {
        return std::to_string(val.a) + "," + val.b;
    }
};

class A : public Reflectable<A> {
public:
    REFLECT_MEMBER(int, a, 1)
    REFLECT_MEMBER(Record, d, Record("record_1"))
    std::string nonreflectable = "nonreflectable";

    explicit A(std::string id) 
        : Reflectable<A>(std::move(id))
        , d("record_1")
    {
        d.a = 2;
        d.b = "hello";
    }
};

template<>
struct TypeTraits<A> {
    static A fromString(const std::string& str) {
        A a("default");
        auto pos = str.find('|');
        if (pos != std::string::npos) {
            a.a = boost::lexical_cast<int>(str.substr(0, pos));
            a.d = TypeTraits<Record>::fromString(str.substr(pos + 1));
        }
        return a;
    }
    
    static std::string toString(const A& val) {
        return std::to_string(val.a) + "|" + TypeTraits<Record>::toString(val.d);
    }
};

// Registry for reflected types
template<typename T>
struct Reflector {
    using MemberMap = std::map<std::string, std::unique_ptr<MemberInfoBase>>;
    
    template<typename MemberInfoT, typename Obj>
    static void add_to_map(MemberMap& members, Obj& obj) {
        members[MemberInfoT::name] = std::make_unique<MemberInfo<typename MemberInfoT::type>>(
            &(obj.*(MemberInfoT::template pointer<Obj>()))
        );
    }

    template<typename Tuple, size_t... Is>
    static MemberMap reflect_impl(T& obj, const Tuple& tuple, std::index_sequence<Is...>) {
        MemberMap members;
        (add_to_map<std::tuple_element_t<Is, Tuple>>(members, obj), ...);
        return members;
    }

    static MemberMap reflect(T& obj) {
        constexpr auto members = T::_reflect_members();
        return reflect_impl(obj, members, 
            std::make_index_sequence<std::tuple_size_v<decltype(members)>>{});
    }
};

// Generic reflection parser
class ReflectionParser {
private:
    static std::vector<std::string> tokenize(const std::string& cmd) {
        std::vector<std::string> tokens;
        std::istringstream iss(cmd);
        std::string token;
        
        while (std::getline(iss, token, ' ')) {
            if (!token.empty()) {
                tokens.push_back(token);
            }
        }
        return tokens;
    }

public:
    static std::string parseAndExecute(const std::string& cmd) {
        auto tokens = tokenize(cmd);
        if (tokens.empty()) return "";

        std::string operation = tokens[0];
        if (tokens.size() < 2) return "";

        std::string pathSpec = tokens[1];
        std::string value;

        if (operation == "set") {
            size_t eqPos = pathSpec.find('=');
            if (eqPos == std::string::npos) return "";
            value = pathSpec.substr(eqPos + 1);
            pathSpec = pathSpec.substr(0, eqPos);
        }

        size_t firstDot = pathSpec.find('.');
        if (firstDot == std::string::npos) return "";

        std::string objectId = pathSpec.substr(0, firstDot);
        std::string memberPath = pathSpec.substr(firstDot + 1);

        A* obj = ObjectRegistry<A>::getObject(objectId);
        if (!obj) {
            std::cerr << "Object not found: " << objectId << std::endl;
            return "";
        }

        // Handle nested paths
        size_t pos = memberPath.find('.');
        if (pos != std::string::npos) {
            std::string baseMember = memberPath.substr(0, pos);
            std::string subPath = memberPath.substr(pos + 1);
            
            auto members = Reflector<A>::reflect(*obj);
            auto it = members.find(baseMember);
            if (it != members.end()) {
                if (baseMember == "d") {
                    return parseAndExecute(operation + " " + objectId + "." + subPath + 
                        (operation == "set" ? "=" + value : ""));
                }
            }
            return "";
        }

        auto members = Reflector<A>::reflect(*obj);
        auto it = members.find(memberPath);
        if (it == members.end()) {
            std::cerr << "Member not found: " << memberPath << std::endl;
            return "";
        }

        if (operation == "set") {
            return it->second->setValue(value) ? value : "";
        } else if (operation == "get") {
            return it->second->getValue();
        }

        return "";
    }
};

} // namespace reflection

int main() {
    using namespace reflection;
    
    A a("test_object");
    
    // Basic get/set for direct members
    assert(ReflectionParser::parseAndExecute("set test_object.a=42") == "42");
    assert(ReflectionParser::parseAndExecute("get test_object.a") == "42");
    
    // Nested member access
    assert(ReflectionParser::parseAndExecute("set test_object.d.a=666") == "666");
    assert(ReflectionParser::parseAndExecute("get test_object.d.a") == "666");
    
    // String member tests
    assert(ReflectionParser::parseAndExecute("set test_object.d.b=hello_world") == "hello_world");
    assert(ReflectionParser::parseAndExecute("get test_object.d.b") == "hello_world");
    
    // Error cases
    assert(ReflectionParser::parseAndExecute("set invalid_object.a=42").empty()); // Invalid object
    assert(ReflectionParser::parseAndExecute("set test_object.invalid=42").empty()); // Invalid member
    assert(ReflectionParser::parseAndExecute("set test_object.a").empty()); // Missing value
    assert(ReflectionParser::parseAndExecute("invalid test_object.a").empty()); // Invalid operation
    assert(ReflectionParser::parseAndExecute("set test_object.nonreflectable=42").empty()); // Non-reflectable member
    
    std::cout << "All tests passed!" << std::endl;
    return 0;
}

