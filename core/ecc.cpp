// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "common.h"
#include "ecc_native.h"

#define ENABLE_MODULE_GENERATOR
#define ENABLE_MODULE_RANGEPROOF

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-function"
#else
    #pragma warning (push, 0) // suppress warnings from secp256k1
#endif

#include "../secp256k1-zkp/src/secp256k1.c"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
    #pragma GCC diagnostic pop
#else
    #pragma warning (pop)
#endif

#ifndef WIN32
#    include <unistd.h>
#    include <fcntl.h>
#endif // WIN32

namespace ECC {

	//void* NoErase(void*, size_t) { return NULL; }

	// Pointer to the 'eraser' function. The pointer should be non-const (i.e. variable that can be changed at run-time), so that optimizer won't remove this.
	void (*g_pfnEraseFunc)(void*, size_t) = memset0/*NoErase*/;

	void SecureErase(void* p, uint32_t n)
	{
		g_pfnEraseFunc(p, n);
	}

	template <typename T>
	void data_cmov_as(T* pDst, const T* pSrc, int nWords, int flag)
	{
		const T mask0 = flag + ~((T)0);
		const T mask1 = ~mask0;

		for (int n = 0; n < nWords; n++)
			pDst[n] = (pDst[n] & mask0) | (pSrc[n] & mask1);
	}

	template void data_cmov_as<uint32_t>(uint32_t* pDst, const uint32_t* pSrc, int nWords, int flag);

	thread_local Mode::Enum g_Mode = Mode::Secure; // default

	Mode::Scope::Scope(Mode::Enum val)
		:m_PrevMode(g_Mode)
	{
		g_Mode = val;
	}

	Mode::Scope::~Scope()
	{
		g_Mode = m_PrevMode;
	}

	std::ostream& operator << (std::ostream& s, const Scalar& x)
	{
		return operator << (s, x.m_Value);
	}

	std::ostream& operator << (std::ostream& s, const Point& x)
	{
		return operator << (s, x.m_X);
	}

	void GenRandom(void* p, uint32_t nSize)
	{
		bool bRet = false;

		// checkpoint?

#ifdef WIN32

		HCRYPTPROV hProv;
		if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_SCHANNEL, CRYPT_VERIFYCONTEXT))
		{
			if (CryptGenRandom(hProv, nSize, (uint8_t*)p))
				bRet = true;
			verify(CryptReleaseContext(hProv, 0));
		}

#else // WIN32

		int hFile = open("/dev/urandom", O_RDONLY);
		if (hFile >= 0)
		{
			if (read(hFile, p, nSize) == nSize)
				bRet = true;

			close(hFile);
		}

#endif // WIN32

		if (!bRet)
			std::ThrowIoError();
	}

	/////////////////////
	// Scalar
	const uintBig Scalar::s_Order = { // fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,
		0xBA,0xAE,0xDC,0xE6,0xAF,0x48,0xA0,0x3B,0xBF,0xD2,0x5E,0x8C,0xD0,0x36,0x41,0x41
	};

	bool Scalar::IsValid() const
	{
		return m_Value < s_Order;
	}

	void Scalar::TestValid() const
	{
		if (!IsValid())
			throw std::runtime_error("invalid scalar");
	}

	Scalar& Scalar::operator = (const Native& v)
	{
		v.Export(*this);
		return *this;
	}

	Scalar& Scalar::operator = (const Zero_&)
	{
		m_Value = Zero;
		return *this;
	}

	Scalar::Native::Native()
    {
        secp256k1_scalar_clear(this);
    }

	Scalar::Native& Scalar::Native::operator = (Zero_)
	{
		secp256k1_scalar_clear(this);
		return *this;
	}

	bool Scalar::Native::operator == (Zero_) const
	{
		return secp256k1_scalar_is_zero(this) != 0;
	}

	bool Scalar::Native::operator == (const Native& v) const
	{
		for (size_t i = 0; i < _countof(d); i++)
			if (d[i] != v.d[i])
				return false;
		return true;
	}

	Scalar::Native& Scalar::Native::operator = (Minus v)
	{
		secp256k1_scalar_negate(this, &v.x);
		return *this;
	}

	bool Scalar::Native::Import(const Scalar& v)
	{
		int overflow;
		secp256k1_scalar_set_b32(this, v.m_Value.m_pData, &overflow);
		return overflow != 0;
	}

	Scalar::Native& Scalar::Native::operator = (const Scalar& v)
	{
		Import(v);
		return *this;
	}

	void Scalar::Native::Export(Scalar& v) const
	{
		secp256k1_scalar_get_b32(v.m_Value.m_pData, this);
	}

	Scalar::Native& Scalar::Native::operator = (uint32_t v)
	{
		secp256k1_scalar_set_int(this, v);
		return *this;
	}

	Scalar::Native& Scalar::Native::operator = (uint64_t v)
	{
		secp256k1_scalar_set_u64(this, v);
		return *this;
	}

	Scalar::Native& Scalar::Native::operator = (Plus v)
	{
		secp256k1_scalar_add(this, &v.x, &v.y);
		return *this;
	}

	Scalar::Native& Scalar::Native::operator = (Mul v)
	{
		secp256k1_scalar_mul(this, &v.x, &v.y);
		return *this;
	}

	void Scalar::Native::SetSqr(const Native& v)
	{
		secp256k1_scalar_sqr(this, &v);
	}

	void Scalar::Native::Sqr()
	{
		SetSqr(*this);
	}

	void Scalar::Native::SetInv(const Native& v)
	{
		secp256k1_scalar_inverse(this, &v);
	}

	void Scalar::Native::Inv()
	{
		SetInv(*this);
	}

	/////////////////////
	// Hash
	Hash::Processor::Processor()
	{
		Reset();
	}

	void Hash::Processor::Reset()
	{
		secp256k1_sha256_initialize(this);
	}

	void Hash::Processor::Write(const void* p, uint32_t n)
	{
		secp256k1_sha256_write(this, (const uint8_t*) p, n);
	}

	void Hash::Processor::Finalize(Value& v)
	{
		secp256k1_sha256_finalize(this, v.m_pData);
		*this << v;
	}

	void Hash::Processor::Write(const char* sz)
	{
		Write(sz, (uint32_t) (strlen(sz) + 1));
	}

	void Hash::Processor::Write(bool b)
	{
		uint8_t n = (false != b);
		Write(n);
	}

	void Hash::Processor::Write(uint8_t n)
	{
		Write(&n, sizeof(n));
	}

	void Hash::Processor::Write(const Scalar& v)
	{
		Write(v.m_Value);
	}

	void Hash::Processor::Write(const Scalar::Native& v)
	{
		NoLeak<Scalar> s_;
		s_.V = v;
		Write(s_.V);
	}

	void Hash::Processor::Write(const Point& v)
	{
		Write(v.m_X);
		Write(v.m_Y);
	}

	void Hash::Processor::Write(const Point::Native& v)
	{
		Write(Point(v));
	}

	void Hash::Mac::Reset(const void* pSecret, uint32_t nSecret)
	{
		secp256k1_hmac_sha256_initialize(this, (uint8_t*)pSecret, nSecret);
	}

	void Hash::Mac::Write(const void* p, uint32_t n)
	{
		secp256k1_hmac_sha256_write(this, (uint8_t*)p, n);
	}

	void Hash::Mac::Finalize(Value& hv)
	{
		secp256k1_hmac_sha256_finalize(this, hv.m_pData);
	}

	/////////////////////
	// Point
	const uintBig Point::s_FieldOrder = { // fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,0xFF,0xFF,0xFC,0x2F
	};

	int Point::cmp(const Point& v) const
	{
		int n = m_X.cmp(v.m_X);
		if (n)
			return n;

		if (m_Y < v.m_Y)
			return -1;
		if (m_Y > v.m_Y)
			return 1;

		return 0;
	}

	Point& Point::operator = (const Native& v)
	{
		v.Export(*this);
		return *this;
	}

	Point& Point::operator = (const Point& v)
	{
		m_X = v.m_X;
		m_Y = v.m_Y;
		return *this;
	}

	Point& Point::operator = (const Commitment& v)
	{
		return operator = (Native(v));
	}

	Point::Native::Native()
    {
        secp256k1_gej_set_infinity(this);
    }

	bool Point::Native::ImportInternal(const Point& v)
	{
		NoLeak<secp256k1_fe> nx;
		if (!secp256k1_fe_set_b32(&nx.V, v.m_X.m_pData))
			return false;

		NoLeak<secp256k1_ge> ge;
		if (!secp256k1_ge_set_xo_var(&ge.V, &nx.V, false != v.m_Y))
			return false;

		secp256k1_gej_set_ge(this, &ge.V);

		return true;
	}

	bool Point::Native::Import(const Point& v)
	{
		if (ImportInternal(v))
			return true;

		*this = Zero;
		return memis0(&v, sizeof(v));
	}

	bool Point::Native::Export(Point& v) const
	{
		if (*this == Zero)
		{
			v.m_X = Zero;
			v.m_Y = false;
			return false;
		}

		NoLeak<secp256k1_gej> dup;
		dup.V = *this;
		NoLeak<secp256k1_ge> ge;
		secp256k1_ge_set_gej(&ge.V, &dup.V);

		// seems like normalization can be omitted (already done by secp256k1_ge_set_gej), but not guaranteed according to docs.
		// But this has a negligible impact on the performance
		secp256k1_fe_normalize(&ge.V.x);
		secp256k1_fe_normalize(&ge.V.y);

		secp256k1_fe_get_b32(v.m_X.m_pData, &ge.V.x);
		v.m_Y = (secp256k1_fe_is_odd(&ge.V.y) != 0);

		return true;
	}

	Point::Native& Point::Native::operator = (Zero_)
	{
		secp256k1_gej_set_infinity(this);
		return *this;
	}

	bool Point::Native::operator == (Zero_) const
	{
		return secp256k1_gej_is_infinity(this) != 0;
	}

	Point::Native& Point::Native::operator = (Minus v)
	{
		secp256k1_gej_neg(this, &v.x);
		return *this;
	}

	Point::Native& Point::Native::operator = (Plus v)
	{
		secp256k1_gej_add_var(this, &v.x, &v.y, NULL);
		return *this;
	}

	Point::Native& Point::Native::operator = (Double v)
	{
		secp256k1_gej_double_var(this, &v.x, NULL);
		return *this;
	}

	Point::Native& Point::Native::operator = (Mul v)
	{
		MultiMac::Casual mc;
		mc.Init(v.x, v.y);

		MultiMac mm;
		mm.m_pCasual = &mc;
		mm.m_Casual = 1;
		mm.Calculate(*this);

		return *this;
	}

	Point::Native& Point::Native::operator += (Mul v)
	{
		return operator += (Native(v));
	}

	/////////////////////
	// Generator
	namespace Generator
	{
		void FromPt(CompactPoint& out, Point::Native& p)
		{
#ifdef ECC_COMPACT_GEN
			secp256k1_ge ge; // used only for non-secret
			secp256k1_ge_set_gej(&ge, &p.get_Raw());
			secp256k1_ge_to_storage(&out, &ge);
#else // ECC_COMPACT_GEN
			out = p.get_Raw();
#endif // ECC_COMPACT_GEN
		}

		void ToPt(Point::Native& p, secp256k1_ge& ge, const CompactPoint& ge_s, bool bSet)
		{
#ifdef ECC_COMPACT_GEN

			secp256k1_ge_from_storage(&ge, &ge_s);

			if (bSet)
				secp256k1_gej_set_ge(&p.get_Raw(), &ge);
			else
				secp256k1_gej_add_ge(&p.get_Raw(), &p.get_Raw(), &ge);

#else // ECC_COMPACT_GEN

			static_assert(sizeof(p) == sizeof(ge_s));

			if (bSet)
				p = (const Point::Native&) ge_s;
			else
				p += (const Point::Native&) ge_s;

#endif // ECC_COMPACT_GEN
		}

		bool CreatePointNnz(Point::Native& out, const uintBig& x)
		{
			Point pt;
			pt.m_X = x;
			pt.m_Y = false;

			return out.Import(pt) && !(out == Zero);
		}

		bool CreatePointNnz(Point::Native& out, Hash::Processor& hp)
		{
			Hash::Value hv;
			hp >> hv;
			return CreatePointNnz(out, hv);
		}

		void CreatePointNnzFromSeed(Point::Native& out, const char* szSeed, Hash::Processor& hp)
		{
			for (hp << szSeed; ; )
				if (CreatePointNnz(out, hp))
					break;
		}

		bool CreatePts(CompactPoint* pPts, Point::Native& gpos, uint32_t nLevels, Hash::Processor& hp)
		{
			Point::Native nums, npos, pt;

			hp << "nums";
			if (!CreatePointNnz(nums, hp))
				return false;

			nums += gpos;

			npos = nums;

			for (uint32_t iLev = 1; ; iLev++)
			{
				pt = npos;

				for (uint32_t iPt = 1; ; iPt++)
				{
					if (pt == Zero)
						return false;

					FromPt(*pPts++, pt);

					if (iPt == nPointsPerLevel)
						break;

					pt += gpos;
				}

				if (iLev == nLevels)
					break;

				for (uint32_t i = 0; i < nBitsPerLevel; i++)
					gpos = gpos * Two;

				npos = npos * Two;
				if (iLev + 1 == nLevels)
				{
					npos = -npos;
					npos += nums;
				}
			}

			return true;
		}

		void SetMul(Point::Native& res, bool bSet, const CompactPoint* pPts, const Scalar::Native::uint* p, int nWords)
		{
			static_assert(8 % nBitsPerLevel == 0, "");
			const int nLevelsPerWord = (sizeof(Scalar::Native::uint) << 3) / nBitsPerLevel;
			static_assert(!(nLevelsPerWord & (nLevelsPerWord - 1)), "should be power-of-2");

			NoLeak<CompactPoint> ge_s;
			NoLeak<secp256k1_ge> ge;

			// iterating in lsb to msb order
			for (int iWord = 0; iWord < nWords; iWord++)
			{
				Scalar::Native::uint n = p[iWord];

				for (int j = 0; j < nLevelsPerWord; j++, pPts += nPointsPerLevel)
				{
					uint32_t nSel = (nPointsPerLevel - 1) & n;
					n >>= nBitsPerLevel;

					/** This uses a conditional move to avoid any secret data in array indexes.
					*   _Any_ use of secret indexes has been demonstrated to result in timing
					*   sidechannels, even when the cache-line access patterns are uniform.
					*  See also:
					*   "A word of warning", CHES 2013 Rump Session, by Daniel J. Bernstein and Peter Schwabe
					*    (https://cryptojedi.org/peter/data/chesrump-20130822.pdf) and
					*   "Cache Attacks and Countermeasures: the Case of AES", RSA 2006,
					*    by Dag Arne Osvik, Adi Shamir, and Eran Tromer
					*    (http://www.tau.ac.il/~tromer/papers/cache.pdf)
					*/

					const CompactPoint* pSel;
					if (Mode::Secure == g_Mode)
					{
						pSel = &ge_s.V;
						for (uint32_t i = 0; i < nPointsPerLevel; i++)
							object_cmov(ge_s.V, pPts[i], i == nSel);
					}
					else
						pSel = pPts + nSel;

					ToPt(res, ge.V, *pSel, bSet);
					bSet = false;
				}
			}
		}

		void SetMul(Point::Native& res, bool bSet, const CompactPoint* pPts, const Scalar::Native& k)
		{
			SetMul(res, bSet, pPts, k.get().d, _countof(k.get().d));
		}

		void GeneratePts(const Point::Native& pt, Hash::Processor& hp, CompactPoint* pPts, uint32_t nLevels)
		{
			while (true)
			{
				Point::Native pt2 = pt;
				if (CreatePts(pPts, pt2, nLevels, hp))
					break;
			}
		}

		void Obscured::Initialize(const Point::Native& pt, Hash::Processor& hp)
		{
			while (true)
			{
				Point::Native pt2 = pt;
				if (!CreatePts(m_pPts, pt2, nLevels, hp))
					continue;

				hp << "blind-scalar";
				Scalar s0;
				hp >> s0.m_Value;
				if (m_AddScalar.Import(s0))
					continue;

				Generator::SetMul(pt2, true, m_pPts, m_AddScalar); // pt2 = G * blind
				FromPt(m_AddPt, pt2);

				m_AddScalar = -m_AddScalar;

				break;
			}
		}

		void Obscured::AssignInternal(Point::Native& res, bool bSet, Scalar::Native& kTmp, const Scalar::Native& k) const
		{
			if (Mode::Secure == g_Mode)
			{
				secp256k1_ge ge;
				ToPt(res, ge, m_AddPt, bSet);

				kTmp = k + m_AddScalar;

				Generator::SetMul(res, false, m_pPts, kTmp);
			}
			else
				Generator::SetMul(res, bSet, m_pPts, k);
		}

		template <>
		void Obscured::Mul<Scalar::Native>::Assign(Point::Native& res, bool bSet) const
		{
			Scalar::Native k2;
			me.AssignInternal(res, bSet, k2, k);
		}

		template <>
		void Obscured::Mul<Scalar>::Assign(Point::Native& res, bool bSet) const
		{
			Scalar::Native k2;
			k2.Import(k); // don't care if overflown (still valid operation)
			me.AssignInternal(res, bSet, k2, k2);
		}

	} // namespace Generator

	/////////////////////
	// MultiMac
	void MultiMac::Prepared::Initialize(const char* szSeed, Hash::Processor& hp)
	{
		Point::Native val;

		for (hp << szSeed; ; )
			if (Generator::CreatePointNnz(val, hp))
			{
				Initialize(val, hp);
				break;
			}
	}

	void MultiMac::Prepared::Initialize(Point::Native& val, Hash::Processor& hp)
	{
		Point::Native npos = val, nums = val * Two;

		for (unsigned int i = 0; i < _countof(m_Fast.m_pPt); i++)
		{
			if (i)
				npos += nums;

			Generator::FromPt(m_Fast.m_pPt[i], npos);
		}

		while (true)
		{
			Hash::Value hv;
			hp << "nums" >> hv;

			if (!Generator::CreatePointNnz(nums, hp))
				continue;

			hp << "blind-scalar";
			Scalar s0;
			hp >> s0.m_Value;
			if (m_Secure.m_Scalar.Import(s0))
				continue;

			npos = nums;
			bool bOk = true;

			for (int i = 0; ; )
			{
				if (npos == Zero)
					bOk = false;
				Generator::FromPt(m_Secure.m_pPt[i], npos);

				if (++i == _countof(m_Secure.m_pPt))
					break;

				npos += val;
			}

			assert(Mode::Fast == g_Mode);
			MultiMac mm;

			const Prepared* ppPrep[] = { this };
			mm.m_ppPrepared = ppPrep;
			mm.m_pKPrep = &m_Secure.m_Scalar;
			FastAux aux;
			mm.m_pAuxPrepared = &aux;
			mm.m_Prepared = 1;

			mm.Calculate(npos);

			npos += nums;
			for (int i = ECC::nBits / Secure::nBits; --i; )
			{
				for (int j = Secure::nBits; j--; )
					nums = nums * Two;
				npos += nums;
			}

			if (npos == Zero)
				bOk = false;

			if (bOk)
			{
				npos = -npos;
				Generator::FromPt(m_Secure.m_Compensation, npos);
				break;
			}
		}
	}

	void MultiMac::Casual::Init(const Point::Native& p)
	{
		if (Mode::Fast == g_Mode)
		{
			m_nPrepared = 1;
			m_pPt[1] = p;
		}
		else
		{
			secp256k1_ge ge;
			Generator::ToPt(m_pPt[0], ge, Context::get().m_Casual.m_Nums, true);

			for (unsigned int i = 1; i < Secure::nCount; i++)
			{
				m_pPt[i] = m_pPt[i - 1];
				m_pPt[i] += p;
			}
		}
	}

	void MultiMac::Casual::Init(const Point::Native& p, const Scalar::Native& k)
	{
		Init(p);
		m_K = k;
	}

	void MultiMac::Reset()
	{
		m_Casual = 0;
		m_Prepared = 0;
	}

	unsigned int GetPortion(const Scalar::Native& k, unsigned int iWord, unsigned int iBitInWord, unsigned int nBitsWnd)
	{
		const Scalar::Native::uint& n = k.get().d[iWord];

		return (n >> (iBitInWord & ~(nBitsWnd - 1))) & ((1 << nBitsWnd) - 1);
	}


	bool GetOddAndShift(const Scalar::Native& k, unsigned int iBitsRemaining, unsigned int nMaxOdd, unsigned int& nOdd, unsigned int& nBitTrg)
	{
		const Scalar::Native::uint* p = k.get().d;
		const uint32_t nWordBits = sizeof(*p) << 3;

		assert(1 & nMaxOdd); // must be odd
		unsigned int nVal = 0;

		while (iBitsRemaining--)
		{
			nVal <<= 1;
			if (nVal > nMaxOdd)
				return true;

			uint32_t n = p[iBitsRemaining / nWordBits] >> (iBitsRemaining & (nWordBits - 1));

			if (1 & n)
			{
				nVal |= 1;
				nOdd = nVal;
				nBitTrg = iBitsRemaining;
			}
		}

		return nVal > 0;
	}

	void MultiMac::Calculate(Point::Native& res) const
	{
		const unsigned int nBitsPerWord = sizeof(Scalar::Native::uint) << 3;

		static_assert(!(nBitsPerWord % Casual::Secure::nBits), "");
		static_assert(!(nBitsPerWord % Prepared::Secure::nBits), "");

		res = Zero;

		unsigned int pTblCasual[nBits];
		unsigned int pTblPrepared[nBits];

		if (Mode::Fast == g_Mode)
		{
			ZeroObject(pTblCasual);
			ZeroObject(pTblPrepared);

			for (int iEntry = 0; iEntry < m_Prepared; iEntry++)
			{
				FastAux& x = m_pAuxPrepared[iEntry];
				unsigned int iBit;
				if (GetOddAndShift(m_pKPrep[iEntry], nBits, Prepared::Fast::nMaxOdd, x.m_nOdd, iBit))
				{
					x.m_nNextItem = pTblPrepared[iBit];
					pTblPrepared[iBit] = iEntry + 1;
				}
			}

			for (int iEntry = 0; iEntry < m_Casual; iEntry++)
			{
				Casual& x = m_pCasual[iEntry];
				unsigned int iBit;
				if (GetOddAndShift(x.m_K, nBits, Casual::Fast::nMaxOdd, x.m_Aux.m_nOdd, iBit))
				{
					x.m_Aux.m_nNextItem = pTblCasual[iBit];
					pTblCasual[iBit] = iEntry + 1;
				}
			}

		}

		NoLeak<secp256k1_ge> ge;
		NoLeak<CompactPoint> ge_s;

		if (Mode::Secure == g_Mode)
			for (int iEntry = 0; iEntry < m_Prepared; iEntry++)
				m_pKPrep[iEntry] += m_ppPrepared[iEntry]->m_Secure.m_Scalar;

		for (unsigned int iBit = ECC::nBits; iBit--; )
		{
			if (!(res == Zero))
				res = res * Two;

			unsigned int iWord = iBit / nBitsPerWord;
			unsigned int iBitInWord = iBit & (nBitsPerWord - 1);

			if (Mode::Fast == g_Mode)
			{
				while (pTblCasual[iBit])
				{
					unsigned int iEntry = pTblCasual[iBit];
					Casual& x = m_pCasual[iEntry - 1];
					pTblCasual[iBit] = x.m_Aux.m_nNextItem;

					assert(1 & x.m_Aux.m_nOdd);
					unsigned int nElem = (x.m_Aux.m_nOdd >> 1) + 1;
					assert(nElem < Casual::Fast::nCount);

					for (; x.m_nPrepared < nElem; x.m_nPrepared++)
					{
						if (1 == x.m_nPrepared)
							x.m_pPt[0] = x.m_pPt[1] * Two;

						x.m_pPt[x.m_nPrepared + 1] = x.m_pPt[x.m_nPrepared] + x.m_pPt[0];
					}

					res += x.m_pPt[nElem];

					unsigned int iBit2;
					if (GetOddAndShift(x.m_K, iBit, Casual::Fast::nMaxOdd, x.m_Aux.m_nOdd, iBit2))
					{
						assert(iBit2 < iBit);

						x.m_Aux.m_nNextItem = pTblCasual[iBit2];
						pTblCasual[iBit2] = iEntry;
					}
				}


				while (pTblPrepared[iBit])
				{
					unsigned int iEntry = pTblPrepared[iBit];
					FastAux& x = m_pAuxPrepared[iEntry - 1];
					pTblPrepared[iBit] = x.m_nNextItem;

					assert(1 & x.m_nOdd);
					unsigned int nElem = (x.m_nOdd >> 1);
					assert(nElem < Prepared::Fast::nCount);

					Generator::ToPt(res, ge.V, m_ppPrepared[iEntry - 1]->m_Fast.m_pPt[nElem], false);

					unsigned int iBit2;
					if (GetOddAndShift(m_pKPrep[iEntry - 1], iBit, Prepared::Fast::nMaxOdd, x.m_nOdd, iBit2))
					{
						assert(iBit2 < iBit);

						x.m_nNextItem = pTblPrepared[iBit2];
						pTblPrepared[iBit2] = iEntry;
					}
				}
			}
			else
			{
				// secure mode
				if (!(iBit & (Casual::Secure::nBits - 1)))
				{
					for (int iEntry = 0; iEntry < m_Casual; iEntry++)
					{
						Casual& x = m_pCasual[iEntry];

						unsigned int nVal = GetPortion(x.m_K, iWord, iBitInWord, Casual::Secure::nBits);

						res += x.m_pPt[nVal]; // cmov seems not needed, since the table is relatively small, and not in global mem (less predicatble addresses)
					}
				}

				if (!(iBit & (Prepared::Secure::nBits - 1)))
				{
					for (int iEntry = 0; iEntry < m_Prepared; iEntry++)
					{
						const Prepared::Secure& x = m_ppPrepared[iEntry]->m_Secure;

						unsigned int nVal = GetPortion(m_pKPrep[iEntry], iWord, iBitInWord, Prepared::Secure::nBits);

						for (unsigned int i = 0; i < _countof(x.m_pPt); i++)
							object_cmov(ge_s.V, x.m_pPt[i], i == nVal);

						Generator::ToPt(res, ge.V, ge_s.V, false);
					}
				}
			}
		}

		if (Mode::Secure == g_Mode)
		{
			for (int iEntry = 0; iEntry < m_Prepared; iEntry++)
			{
				const Prepared::Secure& x = m_ppPrepared[iEntry]->m_Secure;

				Generator::ToPt(res, ge.V, x.m_Compensation, false);
			}

			for (int iEntry = 0; iEntry < m_Casual; iEntry++)
				Generator::ToPt(res, ge.V, Context::get().m_Casual.m_Compensation, false);

		}
	}

	/////////////////////
	// Context
	uint64_t g_pContextBuf[(sizeof(Context) + sizeof(uint64_t) - 1) / sizeof(uint64_t)];

	// Currently - auto-init in global obj c'tor
	Initializer g_Initializer;

#ifndef NDEBUG
	bool g_bContextInitialized = false;
#endif // NDEBUG

	const Context& Context::get()
	{
		assert(g_bContextInitialized);
		return *(Context*) g_pContextBuf;
	}

	void InitializeContext()
	{
		Context& ctx = *(Context*) g_pContextBuf;

		Mode::Scope scope(Mode::Fast);

		Hash::Processor hp;

		// make sure we get the same G,H for different generator kinds
		Point::Native G_raw, H_raw;
		Generator::CreatePointNnzFromSeed(G_raw, "G-gen", hp);
		Generator::CreatePointNnzFromSeed(H_raw, "H-gen", hp);


		ctx.G.Initialize(G_raw, hp);
		ctx.H.Initialize(H_raw, hp);
		ctx.H_Big.Initialize(H_raw, hp);

		Point::Native pt, ptAux2(Zero);

		ctx.m_Ipp.G_.Initialize(G_raw, hp);
		ctx.m_Ipp.H_.Initialize(H_raw, hp);

#define STR_GEN_PREFIX "ip-"
		char szStr[0x20] = STR_GEN_PREFIX;
		szStr[_countof(STR_GEN_PREFIX) + 2] = 0;

		for (uint32_t i = 0; i < InnerProduct::nDim; i++)
		{
			szStr[_countof(STR_GEN_PREFIX) - 1]	= '0' + char(i / 10);
			szStr[_countof(STR_GEN_PREFIX)]		= '0' + char(i % 10);

			for (uint32_t j = 0; j < 2; j++)
			{
				szStr[_countof(STR_GEN_PREFIX) + 1] = '0' + j;
				ctx.m_Ipp.m_pGen_[j][i].Initialize(szStr, hp);

				secp256k1_ge ge;

				if (1 == j)
				{
					Generator::ToPt(pt, ge, ctx.m_Ipp.m_pGen_[j][i].m_Fast.m_pPt[0], true);
					pt = -pt;
					Generator::FromPt(ctx.m_Ipp.m_pGet1_Minus[i], pt);
				} else
					Generator::ToPt(ptAux2, ge, ctx.m_Ipp.m_pGen_[j][i].m_Fast.m_pPt[0], false);
			}
		}

		ptAux2 = -ptAux2;
		hp << "aux2";
		ctx.m_Ipp.m_Aux2_.Initialize(ptAux2, hp);

		ctx.m_Ipp.m_GenDot_.Initialize("ip-dot", hp);

		const MultiMac::Prepared& genericNums = ctx.m_Ipp.m_GenDot_;
		ctx.m_Casual.m_Nums = genericNums.m_Fast.m_pPt[0]; // whatever

		{
			MultiMac_WithBufs<1, 1> mm;
			Scalar::Native& k = mm.m_Bufs.m_pKPrep[0];
			k = Zero;
			for (int i = ECC::nBits; i--; )
			{
				k = k + k;
				if (!(i % MultiMac::Casual::Secure::nBits))
					k = k + 1U;
			}

			k = -k;

			mm.m_Bufs.m_ppPrepared[0] = &ctx.m_Ipp.m_GenDot_;
			mm.m_Prepared = 1;

			mm.Calculate(pt);
			Generator::FromPt(ctx.m_Casual.m_Compensation, pt);
		}

		hp << uint32_t(0); // increment this each time we change signature formula (rangeproof and etc.)

		hp >> ctx.m_hvChecksum;

#ifndef NDEBUG
		g_bContextInitialized = true;
#endif // NDEBUG
	}

	/////////////////////
	// Commitment
	void Commitment::Assign(Point::Native& res, bool bSet) const
	{
		(Context::get().G * k).Assign(res, bSet);
		res += Context::get().H * val;
	}

	/////////////////////
	// Nonce and key generation
	void GenerateNonce(uintBig& res, const uintBig& sk, const uintBig& msg, const uintBig* pMsg2, uint32_t nAttempt /* = 0 */)
	{
		for (uint32_t i = 0; ; i++)
		{
			if (!nonce_function_rfc6979(res.m_pData, msg.m_pData, sk.m_pData, NULL, pMsg2 ? (void*) pMsg2->m_pData : NULL, i))
				continue;

			if (!nAttempt--)
				break;
		}
	}

	void Scalar::Native::GenerateNonce(const uintBig& sk, const uintBig& msg, const uintBig* pMsg2, uint32_t nAttempt /* = 0 */)
	{
		NoLeak<Scalar> s;

		for (uint32_t i = 0; ; i++)
		{
			ECC::GenerateNonce(s.V.m_Value, sk, msg, pMsg2, i);
			if (Import(s.V))
				continue;

			if (!nAttempt--)
				break;
		}
	}

	void Kdf::DeriveKey(Scalar::Native& out, uint64_t nKeyIndex, uint32_t nFlags, uint32_t nExtra) const
	{
		// the msg hash is not secret
		Hash::Value hv;
		Hash::Processor() << nKeyIndex << nFlags << nExtra >> hv;
		out.GenerateNonce(m_Secret.V, hv, NULL);
	}

	/////////////////////
	// Oracle
	void Oracle::Reset()
	{
		m_hp.Reset();
	}

	void Oracle::operator >> (Scalar::Native& out)
	{
		Scalar s; // not secret

		do
			m_hp >> s.m_Value;
		while (out.Import(s));
	}

	/////////////////////
	// Signature
	void Signature::get_Challenge(Scalar::Native& out, const Point::Native& pt, const Hash::Value& msg)
	{
		Oracle() << pt << msg >> out;
	}

	void Signature::MultiSig::GenerateNonce(const Hash::Value& msg, const Scalar::Native& sk)
	{
		NoLeak<Scalar> sk_;
		sk_.V = sk;

		m_Nonce.GenerateNonce(sk_.V.m_Value, msg, NULL);
	}

	void Signature::CoSign(Scalar::Native& k, const Hash::Value& msg, const Scalar::Native& sk, const MultiSig& msig)
	{
		get_Challenge(k, msig.m_NoncePub, msg);
		m_e = k;

		k *= sk;
		k = -k;
		k += msig.m_Nonce;
	}

	void Signature::Sign(const Hash::Value& msg, const Scalar::Native& sk)
	{
		MultiSig msig;
		msig.GenerateNonce(msg, sk);
		msig.m_NoncePub = Context::get().G * msig.m_Nonce;

		Scalar::Native k;
		CoSign(k, msg, sk, msig);
		m_k = k;
	}

	void Signature::get_PublicNonce(Point::Native& pubNonce, const Point::Native& pk) const
	{
		Mode::Scope scope(Mode::Fast);

		pubNonce = Context::get().G * m_k;
		pubNonce += pk * m_e;
	}

	bool Signature::IsValidPartial(const Point::Native& pubNonce, const Point::Native& pk) const
	{
		Point::Native pubN;
		get_PublicNonce(pubN, pk);

		pubN = -pubN;
		pubN += pubNonce;
		return pubN == Zero;
	}

	bool Signature::IsValid(const Hash::Value& msg, const Point::Native& pk) const
	{
		Point::Native pubNonce;
		get_PublicNonce(pubNonce, pk);

		Scalar::Native e2;

		get_Challenge(e2, pubNonce, msg);

		return m_e == Scalar(e2);
	}

	int Signature::cmp(const Signature& x) const
	{
		int n = m_e.cmp(x.m_e);
		if (n)
			return n;

		return m_k.cmp(x.m_k);
	}

	/////////////////////
	// RangeProof
	namespace RangeProof
	{
		void get_PtMinusVal(Point::Native& out, const Point::Native& comm, Amount val)
		{
			out = comm;

			Point::Native ptAmount = Context::get().H * val;

			ptAmount = -ptAmount;
			out += ptAmount;
		}

		// Public
		bool Public::IsValid(const Point::Native& comm, Oracle& oracle) const
		{
			Mode::Scope scope(Mode::Fast);

			if (m_Value < s_MinimumValue)
				return false;

			Point::Native pk;
			get_PtMinusVal(pk, comm, m_Value);

			Hash::Value hv;
			oracle << m_Value >> hv;

			return m_Signature.IsValid(hv, pk);
		}

		void Public::Create(const Scalar::Native& sk, Oracle& oracle)
		{
			assert(m_Value >= s_MinimumValue);
			Hash::Value hv;
			oracle << m_Value >> hv;

			m_Signature.Sign(hv, sk);
		}

		int Public::cmp(const Public& x) const
		{
			int n = m_Signature.cmp(x.m_Signature);
			if (n)
				return n;

			if (m_Value < x.m_Value)
				return -1;
			if (m_Value > x.m_Value)
				return 1;

			return 0;
		}


	} // namespace RangeProof

} // namespace ECC

// Needed for test
void secp256k1_ecmult_gen(const secp256k1_context* pCtx, secp256k1_gej *r, const secp256k1_scalar *a)
{
	secp256k1_ecmult_gen(&pCtx->ecmult_gen_ctx, r, a);
}
