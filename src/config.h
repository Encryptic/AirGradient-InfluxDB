#ifndef __CONFIG_H__
#define __CONFIG_H__

//
// Enables Root Certificate validation based on the `ISRG Root X1`, this will work with InfluxDB Cloud 
// and self-hosted servers if you opt for Let's Encrypt as your certificate provider
//
#define USE_IRSG_ROOT_CERT

//
// Enable HTTP Connection Reuse -- Only enable this if you're trying to reduce conneciton overhead
//
//#define ENABLE_CONNECTION_REUSE

#define U8G2_TOP

#endif //__CONFIG_H__
