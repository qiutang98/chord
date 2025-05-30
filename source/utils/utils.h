#pragma once

#include <pch.h>

#include <utils/noncopyable.h>

#define ENABLE_LOG
#define ENABLE_RESTRICT
#define ENABLE_NODISCARD
#define ENABLE_DEPRECATED
#define ENABLE_LIKELY


#if defined(_DEBUG) || defined(DEBUG)
	#define CHORD_DEBUG 1
#else
	#define CHORD_DEBUG 0
#endif

#ifdef ENABLE_NODISCARD
	#define CHORD_NODISCARD [[nodiscard]]
#else 
	#define CHORD_NODISCARD
#endif

#ifdef ENABLE_DEPRECATED
	#define CHORD_DEPRECATED(msg) [[deprecated(msg)]]
#else 
	#define CHORD_DEPRECATED(msg)
#endif

// Optional restrict keyword for memory optimize.
#ifdef ENABLE_RESTRICT
	#define CHORD_RESTRICT __restrict
#else
	#define CHORD_RESTRICT 
#endif 

#ifdef ENABLE_LIKELY
	#define CHORD_LIKELY [[likely]]
	#define CHORD_UNLIKELY [[unlikely]]
#else
	#define CHORD_LIKELY
	#define CHORD_UNLIKELY
#endif // ENABLE_LIKELY

#define CHORD_DEBUG_BREAK chord::reportBreakpoint();
#define CHORD_CRASH CHORD_DEBUG_BREAK chord::reportCrash();

#ifdef ENABLE_LOG
	#define chord_macro_sup_enableLogOnly(x) x
#else
	#define chord_macro_sup_enableLogOnly(x)
#endif

#define ASSERT_POWER_OF_TWO(c) assert(c && (!(c & (c-1))))

#if CHORD_DEBUG
	#define chord_macro_sup_debugOnly(x) x
	#define chord_macro_sup_checkPrintContent(x, p) \
	do { if(!(x)) { p("Check '{3}' failed in function '{1}' at line {0} on file '{2}'.", __LINE__, __FUNCTION__, __FILE__, #x); } } while(0)

	#define chord_macro_sup_checkMsgfPrintContent(x, p, ...) \
	do { if(!(x)) { p("Assert '{4}' failed with message: '{3}' in function '{1}' at line {0} on file '{2}'.", __LINE__, __FUNCTION__, __FILE__, std::format(__VA_ARGS__), #x); } } while(0)
#else
	#define chord_macro_sup_debugOnly(x)
	#define chord_macro_sup_checkPrintContent(x, p) do { if(!(x)) { p("Check '{0}' failed.", #x); } } while(0)
	#define chord_macro_sup_checkMsgfPrintContent(x, p, ...) do { if(!(x)) { p("Assert '{0}' failed with message '{1}'.", #x, __VA_ARGS__); } } while(0)
#endif

#define chord_macro_sup_ensureMsgfContent(x, p, ...) \
do { static bool b = false; if(!b && !(x)) { b = true; p("Ensure '{4}' failed with message '{3}' in function '{1}' at line #{0} on file '{2}'.", __LINE__, __FUNCTION__, __FILE__, std::format(__VA_ARGS__), #x); CHORD_DEBUG_BREAK } } while(0)

#define chord_macro_sup_checkEntryContent(x) x("Check entry in function '{1}' at line {0} on file '{2}'.", __LINE__, __FUNCTION__, __FILE__)
#define chord_macro_sup_unimplementedContent(x) x("Unimplemented code entry in function '{1}' at line {0} on file '{2}'.", __LINE__, __FUNCTION__, __FILE__)

// Operator enum flags support macro for enum class.
#define ENUM_CLASS_FLAG_OPERATORS(T)                                                 \
    constexpr inline T operator |(T a, T b) { return T(uint32_t(a) | uint32_t(b)); } \
    constexpr inline T operator &(T a, T b) { return T(uint32_t(a) & uint32_t(b)); } \
    constexpr inline T operator ~(T a     ) { return T(~uint32_t(a)); }              \
    constexpr inline bool operator  !(T  a) { return uint32_t(a) == 0; }             \
    constexpr inline bool operator ==(T a, uint32_t b) { return uint32_t(a) == b; }  \
    constexpr inline bool operator !=(T a, uint32_t b) { return uint32_t(a) != b; }

#define DECLARE_SUPER_TYPE(Parent) using Super = Parent

#define ARCHIVE_DECLARE                                                                  \
	friend class cereal::access;                                                         \
	template<class Ar>                                                                   \
	void serialize(Ar& ar, std::uint32_t const version);

#define ARCHIVE_NVP_DEFAULT(Member) ar(cereal::make_nvp(#Member, Member))

// Version and type registry.
#define ASSET_ARCHIVE_IMPL_BASIC(AssetNameXX, Version)                                   \
	CEREAL_CLASS_VERSION(chord::AssetNameXX, Version);                                   \
	CEREAL_REGISTER_TYPE_WITH_NAME(chord::AssetNameXX, "chord::"#AssetNameXX);

// Virtual children class.
#define registerClassMemberInherit(AssetNameXX, AssetNamePP)                             \
	ASSET_ARCHIVE_IMPL_BASIC(AssetNameXX, chord::kAssetVersion);                         \
	CEREAL_REGISTER_POLYMORPHIC_RELATION(chord::AssetNamePP, chord::AssetNameXX)         \
	template<class Ar>                                                                   \
	void chord::AssetNameXX::serialize(Ar& ar, std::uint32_t const version) {            \
	ar(cereal::base_class<chord::AssetNamePP>(this));

// Baisc class.
#define registerClassMember(AssetNameXX)                                                 \
	ASSET_ARCHIVE_IMPL_BASIC(AssetNameXX, chord::kAssetVersion);                         \
	template<class Ar>                                                                   \
	void chord::AssetNameXX::serialize(Ar& ar, std::uint32_t const version)

// Achive enum class type.
#define ARCHIVE_ENUM_CLASS(value)                     \
	{ size_t enum__type__##value = (size_t)value;     \
	ARCHIVE_NVP_DEFAULT(enum__type__##value);         \
	value = (decltype(value))(enum__type__##value); }

#define registerPODClassMember(AssetNameXX)                                              \
	CEREAL_CLASS_VERSION(chord::AssetNameXX, chord::kAssetVersion);                      \
	template<class Ar>                                                                   \
	void chord::AssetNameXX::serialize(Ar& ar, std::uint32_t const version)

#define REGISTER_BODY_DECLARE(...)  \
	ARCHIVE_DECLARE                 \
	RTTR_ENABLE(__VA_ARGS__);       \
	RTTR_REGISTRATION_FRIEND();


#define CHORD_DIVIDE_AND_ROUND_UP(A, B) (((A) + (B) - 1) / (B))

namespace chord
{
	class ImageLdr2D;
	class Application;

	// Signed type.
	using int32  = int32_t;
	using int64  = int64_t;
	using int8   = int8_t;
	using int16  = int16_t;

	// Unsigned type.
	using uint32 = uint32_t;
	using uint64 = uint64_t;
	using uint8  = uint8_t;
	using uint16 = uint16_t;

	// Basic type platform memory check.
	static_assert(
		sizeof(int32)  == 4 && 
		sizeof(uint32) == 4 &&
		sizeof(int64)  == 8 && 
		sizeof(uint64) == 8 &&
		sizeof(int8)   == 1 &&
		sizeof(uint8)  == 1);

	// Asset version control all asset.
	extern const uint32 kAssetVersion;

	class ApplicationTickData
	{
	public:
		uint64 tickCount;
		double totalTime;

		double fps;
		double dt;

		// Update persecond fps this frame or not.
		bool bFpsUpdatedPerSecond;
		double fpsUpdatedPerSecond;
	};

	// 
	enum class ERuntimePeriod
	{
		Initing = 0,
		Ticking,
		BeforeReleasing,
		Releasing,

		MAX
	};

	// Alias string to notify user know current is u8str instead std::u8string because it is hard to use.
	class u16str
	{
	public:
		u16str() = default;

		explicit u16str(const char* u8encode)
			: m_u16str(utf8::utf8to16(u8encode))
		{
		}

		explicit u16str(const std::string& u8encode)
			: m_u16str(utf8::utf8to16(u8encode))
		{
		}
		
		u16str(const std::u16string& u16str)
			: m_u16str(u16str)
		{

		}

		bool operator==(const u16str& lhs) const
		{
			return m_u16str == lhs.m_u16str;
		}

		bool operator<(const u16str& lhs) const
		{
			return m_u16str < lhs.m_u16str;
		}

		operator const std::u16string&() const
		{
			return m_u16str;
		}


		operator std::u16string&()
		{
			return m_u16str;
		}

		bool empty() const 
		{
			return m_u16str.empty();
		}

		std::string u8() const
		{
			return utf8::utf16to8(m_u16str);
		}

		auto& data() 
		{
			return m_u16str;
		}

		const auto& data() const
		{
			return m_u16str;
		}

		const std::u16string& u16() const
		{
			return m_u16str;
		}

		// System code page string, we use filesystem path help.
		std::string str() const 
		{
			std::filesystem::path path = u16();
			return path.string();
		}

		uint64 hash() const 
		{
			return std::hash<std::u16string>{}(m_u16str);
		}

	private:
		std::u16string m_u16str;

	public:
		template<class Ar> void save(Ar& ar) const
		{
			std::string u8 = utf8::utf16to8(m_u16str);
			ar(u8);
		}

		template<class Ar> void load(Ar& ar)
		{
			std::string u8;
			ar(u8);

			m_u16str = utf8::utf8to16(u8);
		}
	};

	template<typename T>
	class RegisterManager
	{
	public:
		void add(T& in)
		{
			m_registers.push_back(in);
		}

		bool remove(const T& in)
		{
			size_t i = 0;
			for (auto& iter : m_registers)
			{
				if (iter == in)
				{
					break;
				}

				i++;
			}

			if (i >= m_registers.size())
			{
				return false;
			}

			m_registers[i] = std::move(m_registers.back());
			m_registers.pop_back();

			return true;
		}

		void loop(std::function<void(T& r)>&& f)
		{
			for (auto& iter : m_registers)
			{
				f(iter);
			}
		}

		void clear()
		{
			m_registers.clear();
		}

	private:
		std::vector<T> m_registers;
	};

	class SizedBuffer
	{
	public:
		SizedBuffer() = default;
		SizedBuffer(uint64 inSize, void* inPtr)
			: size(inSize)
			, ptr(inPtr)
		{

		}

		template<typename T>
		SizedBuffer(const std::vector<T>& blob)
			: size(blob.size() * sizeof(T)), ptr((void*)blob.data())
		{

		}

		uint64 size = 0;
		void* ptr = nullptr;

		bool isValid()
		{
			return ptr != nullptr && size > 0;
		}
	};

	// Interface for all resource used in application.
	class IResource : NonCopyable 
	{
	private:
		const uint64 m_id;

	public:
		explicit IResource();

		uint64 hash() const
		{
			return m_id;
		}

		virtual ~IResource();
	};
	using ResourceRef = std::shared_ptr<IResource>;

	// DeletionQueue used for shared_ptr resource lazy release.
	// FIFO
	class DeleteQueue
	{
	private:
		std::vector<ResourceRef> m_pendingQueue;

	public:
		~DeleteQueue();

		void push(ResourceRef inT);
		void clear();
	};

	template<typename T, size_t Dim>
	class StaticArray
	{
	public:
		void push(const T& inT) { m_data[m_size] = inT; m_size ++; }
		const bool empty() const { return m_size == 0; }
		const auto size() const { return m_size; }

		const auto& getArray() const { return m_data; }
		const T* data() const { return m_size > 0 ? m_data.data() : nullptr; }

	private:
		size_t m_size = 0;
		std::array<T, Dim> m_data = {};
	};

	// CPU cache line size, set 64 bytes here.
	constexpr uint32 kCpuCachelineSize = std::hardware_destructive_interference_size;

    // Zero object memory.
    template<typename T> static inline void zeroMemory(T& data)
	{
		static_assert(std::is_object_v<T>);
	    ::memset(&data, 0, sizeof(T));
	}

	extern void setConsoleTitle(const std::string& title);
	extern void setConsoleUtf8();

	// Set first valid font as console font.
	struct ConsoleFontConfig
	{
		std::wstring name;
		uint32 width;
		uint32 height;
	};
	extern void setConsoleFont(const std::vector<ConsoleFontConfig>& fontTypes);

	static inline bool same(const char* a, const char* b)
	{
		return strcmp(a, b) == 0;
	}

	template<typename T>
	constexpr const char* getTypeName()
	{
		return typeid(T).name();
	}

	template<typename T>
	constexpr size_t getTypeHash()
	{
		return typeid(T).hash_code();
	}

	template < typename T, size_t N >
	size_t countof(T(&arr)[N])
	{
		return std::extent<T[N]>::value;
	}

	extern std::wstring_view getCurrentThreadName();
	extern void namedCurrentThread(const std::wstring& name);

	extern void reportCrash();
	extern void reportBreakpoint();

	// 
	extern bool isDebuggerAttach();
	extern bool createDump(bool bFullDump, const std::wstring& dumpFilePath);

	extern bool loadFile(const std::filesystem::path& path, std::vector<char>& binData, const char* mode);
	extern bool storeFile(const std::filesystem::path& path, const uint8* ptr, uint32 size, const char* mode);

	static inline uint64 hashCombine(uint64 lhs, uint64 rhs)
	{
		lhs ^= rhs + 0x9e3779b9 + (lhs << 6) + (lhs >> 2);
		return lhs;
	}

	using UUID = std::string;
	CHORD_NODISCARD extern UUID generateUUID();

	// Build two path relative path, remove first "\" and "/" char.
	inline static std::u16string buildRelativePath(
		const std::filesystem::path& shortPath,
		const std::filesystem::path& longPath)
	{
		const std::u16string shortPath16 = std::filesystem::absolute(shortPath).u16string();
		const std::u16string longPath16 = std::filesystem::absolute(longPath).u16string();

		auto result = longPath16.substr(shortPath16.size());
		if (result.starts_with(u"\\") || result.starts_with(u"/"))
		{
			result = result.erase(0, 1);
		}
		return result;
	}

	static inline bool isPOT(uint32 n)
	{
		return (n & (n - 1)) == 0;
	}

	// Get texture mipmap level count.
	static inline uint32 getMipLevelsCount(uint32 texWidth, uint32 texHeight)
	{
		return static_cast<uint32>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
	}

	template<typename T> inline T divideRoundingUp(T x, T y)
	{
		return (x + y - (T)1) / y;
	}

	template<typename T> inline T alignRoundingUp(T x, T y)
	{
		return ((x + y - (T)1) / y) * y;
	}

	extern std::filesystem::path buildStillNonExistPath(const std::filesystem::path& p);


	// From https://github.com/TheCherno/Hazel/blob/master/Hazel/src/Hazel/Math/Math.cpp
	extern bool decomposeTransform(const math::dmat4& transform, math::dvec3& translation, math::dvec3& rotation, math::dvec3& scale);

	static inline bool hasFlag(auto a, auto b)
	{
		return (a & b) == b;
	}

	static inline int absFloor(const double value)
	{
		return value >= 0.f ? int(floor(value)) : int(ceil(value));
	}
	
	static inline uint32 getNextPOT(uint32 v)
	{
		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v++;
		return v;
	}

	// halton sequence compute.
	static inline float halton(uint64_t index, uint64_t base)
	{
		float f = 1; float r = 0;
		while (index > 0)
		{
			f = f / static_cast<float>(base);
			r = r + f * (index % base);
			index = index / base;
		}
		return r;
	}

	// halton 2d sequence compute.
	static inline math::vec2 halton2D(uint64_t index, uint64_t baseA, uint64_t baseB)
	{
		return math::vec2(halton(index, baseA), halton(index, baseB));
	}

	// halton 2d 2/3
	static inline math::vec2 halton2D_23(uint64_t index)
	{
		return math::vec2(halton(index, 2), halton(index, 3));
	}

	extern uint64 requireUniqueId();

	extern math::mat4 infiniteInvertZPerspectiveRH_ZO(float aspect, float fovy, float zNear);

	static inline bool isDigitString(const std::string& s)
	{
		return std::all_of(std::begin(s), std::end(s), [](unsigned char c) { return std::isdigit(c); });
	}

	static inline std::string formatTimestamp(const std::chrono::system_clock::time_point& time, const std::string& format = "_%Y_%m_%d_%H_%M_%S")
	{
		std::time_t tt = std::chrono::system_clock::to_time_t(time);
		std::tm tm = *std::localtime(&tt);
		std::stringstream ss;
		ss << std::put_time(&tm, format.c_str());
		return ss.str();
	}

	// ScopeExit use RAII to call a function when the object goes out of scope.
	// Useful for cleanup code.
	template<typename Callback>
	class ScopeExit final
	{
		static_assert(std::is_invocable_v<Callback>, "Callback must be a function type.");
	public:
		~ScopeExit() { m_func(); }

		explicit ScopeExit(Callback func)
			: m_func(func)
		{

		}

	private:
		Callback m_func;
	};

	// Helper function to create a ScopeExit object.
	template<typename Callback>
	static inline ScopeExit<Callback> makeScopeExit(Callback func)
	{
		return ScopeExit(func);
	}

	// Boost function_traits
	template <typename T>
	struct function_traits;

	template <typename Ret, typename... Args>
	struct function_traits<Ret(Args...)>
	{
		using return_type = Ret;
		using args_tuple = std::tuple<Args...>;
		static constexpr size_t arity = sizeof...(Args);

		template <size_t N>
		struct argument
		{
			static_assert(N < arity, "Index out of range");
			using type = std::tuple_element_t<N, args_tuple>;
		};
	};

	// Generic function.
	template <typename Ret, typename... Args>
	struct function_traits<Ret(*)(Args...)> : function_traits<Ret(Args...)> {};

	// Class member function.
	template <typename ClassType, typename Ret, typename... Args>
	struct function_traits<Ret(ClassType::*)(Args...)> : function_traits<Ret(Args...)> {};

	// Class const member function.
	template <typename ClassType, typename Ret, typename... Args>
	struct function_traits<Ret(ClassType::*)(Args...) const> : function_traits<Ret(Args...)> {};

	// Callable object.
	template <typename T>
	struct function_traits : function_traits<decltype(&T::operator())> {};

	// Alias.
	template <typename T>
	using function_return_t = typename function_traits<T>::return_type;

	template <typename T, size_t N>
	using function_argument_t = typename function_traits<T>::template argument<N>::type;
}
