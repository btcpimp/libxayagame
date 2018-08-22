// Copyright (C) 2018 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "uint256.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace xaya
{
namespace
{

TEST (Uint256Tests, FromValidHex)
{
  uint256 obj;
  ASSERT_TRUE (obj.FromHex ("42" + std::string (60, '0') + "aF"));

  auto it = obj.begin ();
  EXPECT_EQ (*it++, 0x42);
  for (size_t i = 1; i < 31; ++i)
    EXPECT_EQ (*it++, 0x00);
  EXPECT_EQ (*it++, 0xAF);
  EXPECT_EQ (it, obj.end ());
}

TEST (Uint256Tests, FromInvalidHex)
{
  uint256 obj;

  EXPECT_FALSE (obj.FromHex (""));
  EXPECT_FALSE (obj.FromHex ("00"));
  EXPECT_FALSE (obj.FromHex (std::string (66, '0')));
  EXPECT_FALSE (obj.FromHex ("xx" + std::string (62, '0')));
}

TEST (Uint256Tests, ToHex)
{
  /* We verify the exact data for FromHex above.  So by doing a round-trip,
     we also make sure that ToHex works correctly (and not just that the
     round-trip works).  */

  const std::string hex("02" + std::string (60, '0') + "af");

  uint256 obj;
  ASSERT_TRUE (obj.FromHex (hex));

  EXPECT_EQ (obj.ToHex (), hex);
}

TEST (Uint256Tests, Comparison)
{
  const std::string strLow(std::string (62, '0') + "ff");
  const std::string strHigh("ff" + std::string (62, '0'));

  uint256 low1, low2;
  ASSERT_TRUE (low1.FromHex (strLow));
  ASSERT_TRUE (low2.FromHex (strLow));

  uint256 high;
  ASSERT_TRUE (high.FromHex (strHigh));

  EXPECT_TRUE (low1 == low2);
  EXPECT_FALSE (low1 == high);

  EXPECT_TRUE (low1 < high);
  EXPECT_FALSE (low1 < low2);
  EXPECT_FALSE (high < low1);
}

} // anonymous namespace
} // namespace xaya
