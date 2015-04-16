#include "PlexTest.h"
#include "PlexAES.h"

const char* key = "ff0a27dc-338c-4e8a-9590-0dddb5f0cbfe";

void
TestPlexAES(CStdString data)
{
  CPlexAES aes(key);
  CStdString encrypted = aes.encrypt(data);
  int expectedLen = data.length();
  int rem = data.length() % AES_BLOCK_SIZE;
  if (rem != 0)
    expectedLen += AES_BLOCK_SIZE - rem;

  EXPECT_EQ(expectedLen, encrypted.length());

  CStdString decrypted = aes.decrypt(encrypted);
  EXPECT_STREQ(data.c_str(), decrypted.c_str());

  EXPECT_EQ(strlen(decrypted.c_str()), strlen(data.c_str()));
}

TEST(PlexAES, encryptDecryptSmallStr)
{
  TestPlexAES("foo");
}

TEST(PlexAES, encryptDecryptEmpty)
{
  TestPlexAES("");
}

TEST(PlexAES, encryptDecryptToken)
{
  TestPlexAES("AAABzKSNM6RAFqPxJDuG");
}

TEST(PlexAES, encryptALotOfData)
{
  TestPlexAES(testItem_movie);
}
