/**
*
*
*
*
*
**/

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "base64.h"

#define BASE64_IGNORE  -1
#define BASE64_PAD   -2

void base64_set_line_length(struct base64_context_st *context, const int length)
{
    context->line_length = (length / 4) * 4;
}

void base64_set_line_separator(struct base64_context_st *context, \
		const char *pLineSeparator)
{
    context->line_sep_len = snprintf(context->line_separator, \
			sizeof(context->line_separator), "%s", pLineSeparator);
}

void base64_init_ex(struct base64_context_st *context, const int nLineLength, \
		const unsigned char chPlus, const unsigned char chSplash, \
		const unsigned char chPad)
{
      int i;

      memset(context, 0, sizeof(struct base64_context_st));

      context->line_length = nLineLength;
      context->line_separator[0] = '\n';
      context->line_separator[1] = '\0';
      context->line_sep_len = 1;

      // 0..25 -> 'A'..'Z'
      for (i=0; i<=25; i++)
      {
         context->valueToChar[i] = (char)('A'+i);
      }
      // 26..51 -> 'a'..'z'
      for (i=0; i<=25; i++ )
      {
         context->valueToChar[i+26] = (char)('a'+i);
      }
      // 52..61 -> '0'..'9'
      for (i=0; i<=9; i++ )
      {
         context->valueToChar[i+52] = (char)('0'+i);
      }
      context->valueToChar[62] = chPlus;
      context->valueToChar[63] = chSplash;

      memset(context->charToValue, BASE64_IGNORE, sizeof(context->charToValue));
      for (i=0; i<64; i++ )
      {
         context->charToValue[context->valueToChar[i]] = i;
      }

      context->pad_ch = chPad;
      context->charToValue[chPad] = BASE64_PAD;
}

int base64_get_encode_length(struct base64_context_st *context, const int nSrcLen)
{
   int outputLength;

   outputLength = ((nSrcLen + 2) / 3) * 4;

   if (context->line_length != 0)
   {
       int lines =  (outputLength + context->line_length - 1) / 
			context->line_length - 1;
       if ( lines > 0 )
       {
          outputLength += lines  * context->line_sep_len;
       }
   }

   return outputLength;
}

char *base64_encode_ex(struct base64_context_st *context, const char *src, \
		const int nSrcLen, char *dest, int *dest_len, const bool bPad)
{
  int linePos;
  int leftover;
  int combined;
  char *pDest;
  int c0, c1, c2, c3;
  unsigned char *pRaw;
  unsigned char *pEnd;
  const char *ppSrcs[2];
  int lens[2];
  char szPad[3];
  int k;
  int loop;

  if (nSrcLen <= 0)
  {
       *dest = '\0';
       *dest_len = 0;
       return dest;
  }

  linePos = 0;
  lens[0] = (nSrcLen / 3) * 3;
  lens[1] = 3;
  leftover = nSrcLen - lens[0];
  ppSrcs[0] = src;
  ppSrcs[1] = szPad;

  szPad[0] = szPad[1] = szPad[2] = '\0';
  switch (leftover)
  {
      case 0:
      default:
           loop = 1;
           break;
      case 1:
           loop = 2;
           szPad[0] = src[nSrcLen-1];
           break;
      case 2:
           loop = 2;
           szPad[0] = src[nSrcLen-2];
           szPad[1] = src[nSrcLen-1];
           break;
  }

  pDest = dest;
  for (k=0; k<loop; k++)
  {
      pEnd = (unsigned char *)ppSrcs[k] + lens[k];
      for (pRaw=(unsigned char *)ppSrcs[k]; pRaw<pEnd; pRaw+=3)
      {
         linePos += 4;
         if (linePos > context->line_length)
         {
            if (context->line_length != 0)
            {
               memcpy(pDest, context->line_separator, context->line_sep_len);
               pDest += context->line_sep_len;
            }
            linePos = 4;
         }

         combined = ((*pRaw) << 16) | ((*(pRaw+1)) << 8) | (*(pRaw+2));

         c3 = combined & 0x3f;
         combined >>= 6;
         c2 = combined & 0x3f;
         combined >>= 6;
         c1 = combined & 0x3f;
         combined >>= 6;
         c0 = combined & 0x3f;

         *pDest++ = context->valueToChar[c0];
         *pDest++ = context->valueToChar[c1];
         *pDest++ = context->valueToChar[c2];
         *pDest++ = context->valueToChar[c3];
      }
  }

  *pDest = '\0';
  *dest_len = pDest - dest;

  switch (leftover)
  {
     case 0:
     default:
        // nothing to do
        break;
     case 1:
        if (bPad)
        {
           *(pDest-1) = context->pad_ch;
           *(pDest-2) = context->pad_ch;
        }
        else
        {
           *(pDest-2) = '\0';
           *dest_len -= 2;
        }
        break;
     case 2:
        if (bPad)
        {
           *(pDest-1) = context->pad_ch;
        }
        else
        {
           *(pDest-1) = '\0';
           *dest_len -= 1;
        }
        break;
  }

  return dest;
}

char *base64_decode_auto(struct base64_context_st *context, const char *src, \
		const int nSrcLen, char *dest, int *dest_len)
{
	int nRemain;
	int nPadLen;
	int nNewLen;
	char tmpBuff[256];
	char *pBuff;

	nRemain = nSrcLen % 4;
	if (nRemain == 0)
	{
		return base64_decode(context, src, nSrcLen, dest, dest_len);
	}

	nPadLen = 4 - nRemain;
	nNewLen = nSrcLen + nPadLen;
	if (nNewLen <= sizeof(tmpBuff))
	{
		pBuff = tmpBuff;
	}
	else
	{
		pBuff = (char *)malloc(nNewLen);
		if (pBuff == NULL)
		{
			fprintf(stderr, "Can't malloc %d bytes\n", \
				nSrcLen + nPadLen + 1);
			*dest_len = 0;
			*dest = '\0';
			return dest;
		}
	}

	memcpy(pBuff, src, nSrcLen);
	memset(pBuff + nSrcLen, context->pad_ch, nPadLen);

	base64_decode(context, pBuff, nNewLen, dest, dest_len);

	if (pBuff != tmpBuff)
	{
		free(pBuff);
	}

	return dest;
}

char *base64_decode(struct base64_context_st *context, const char *src, \
		const int nSrcLen, char *dest, int *dest_len)
{
      int cycle;
      int combined;
      int dummies;
      int value;
      unsigned char *pSrc;
      unsigned char *pSrcEnd;
      char *pDest;

      cycle = 0;
      combined = 0;
      dummies = 0;
      pDest = dest;
      pSrcEnd = (unsigned char *)src + nSrcLen;
      for (pSrc=(unsigned char *)src; pSrc<pSrcEnd; pSrc++)
      {
         value = context->charToValue[*pSrc];
         switch (value)
         {
            case BASE64_IGNORE:
               // e.g. \n, just ignore it.
               break;
            case BASE64_PAD:
               value = 0;
               dummies++;
               // fallthrough
            default:
               /* regular value character */
               switch (cycle)
               {
                  case 0:
                     combined = value;
                     cycle = 1;
                     break;
                  case 1:
                     combined <<= 6;
                     combined |= value;
                     cycle = 2;
                     break;
                  case 2:
                     combined <<= 6;
                     combined |= value;
                     cycle = 3;
                     break;
                  case 3:
                     combined <<= 6;
                     combined |= value;
                     *pDest++ = (char)(combined >> 16);
                     *pDest++ = (char)((combined & 0x0000FF00) >> 8);
                     *pDest++ = (char)(combined & 0x000000FF);

                     cycle = 0;
                     break;
               }
               break;
         }
      }

      if (cycle != 0)
      {
         *dest = '\0';
         *dest_len = 0;
         fprintf(stderr, "Input to decode not an even multiple of " \
		"4 characters; pad with %c\n", context->pad_ch);
         return dest;
      }

      *dest_len = (pDest - dest) - dummies;
      *(dest + (*dest_len)) = '\0';

      return dest;
}

