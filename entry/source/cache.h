
#ifndef _CACHE_H_
#define _CACHE_H_

void DCFlushRange(void *startaddress,unsigned int len);
void DCFlushRangeNoSync(void *startaddress,unsigned int len);
void ICInvalidateRange(void *startaddress,unsigned int len);

#endif