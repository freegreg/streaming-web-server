#include <winsock2.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <WS2tcpip.h>
// Link with Iphlpapi.lib
#pragma comment(lib, "IPHLPAPI.lib")

#define WORKING_BUFFER_SIZE 15000
#define MAX_TRIES 3

#define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
#define FREE(x) HeapFree(GetProcessHeap(), 0, (x))

/* Note: could also use malloc() and free() */

void printLocalIP(int ipFamily)
{

	/* Declare and initialize variables */

	DWORD dwSize = 0;
	DWORD dwRetVal = 0;

	unsigned int i = 0;

	// Set the flags to pass to GetAdaptersAddresses
	ULONG flags = GAA_FLAG_INCLUDE_PREFIX;

	// default to unspecified address family (both)
	ULONG family = AF_UNSPEC;

	LPVOID lpMsgBuf = NULL;

	PIP_ADAPTER_ADDRESSES pAddresses = NULL;
	ULONG outBufLen = 0;
	ULONG Iterations = 0;

	PIP_ADAPTER_ADDRESSES pCurrAddresses = NULL;
	PIP_ADAPTER_UNICAST_ADDRESS pUnicast = NULL;
	PIP_ADAPTER_ANYCAST_ADDRESS pAnycast = NULL;
	PIP_ADAPTER_MULTICAST_ADDRESS pMulticast = NULL;
	IP_ADAPTER_DNS_SERVER_ADDRESS *pDnServer = NULL;
	IP_ADAPTER_PREFIX *pPrefix = NULL;

	if (ipFamily == 4)
		family = AF_INET;
	else if (ipFamily == 6)
		family = AF_INET6;

	if (family == AF_INET)
		printf("AF_INET\n");
	if (family == AF_INET6)
		printf("AF_INET6\n");
	if (family == AF_UNSPEC)
		printf("AF_UNSPEC\n\n");

	// Allocate a 15 KB buffer to start with.
	outBufLen = WORKING_BUFFER_SIZE;

	do {

		pAddresses = (IP_ADAPTER_ADDRESSES *)MALLOC(outBufLen);
		if (pAddresses == NULL) {
			printf
			("Memory allocation failed for IP_ADAPTER_ADDRESSES struct\n");
			exit(1);
		}

		dwRetVal =
			GetAdaptersAddresses(family, flags, NULL, pAddresses, &outBufLen);

		if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
			FREE(pAddresses);
			pAddresses = NULL;
		}
		else {
			break;
		}

		Iterations++;

	} while ((dwRetVal == ERROR_BUFFER_OVERFLOW) && (Iterations < MAX_TRIES));

	if (dwRetVal == NO_ERROR) {
		// If successful, output some information from the data we received
		pCurrAddresses = pAddresses;
		while (pCurrAddresses) {
			//printf("\tLength of the IP_ADAPTER_ADDRESS struct: %ld\n",
			//	pCurrAddresses->Length);
			//printf("\tIfIndex (IPv4 interface): %u\n", pCurrAddresses->IfIndex);
			//printf("\tAdapter name: %s\n", pCurrAddresses->AdapterName);
			
			printf("\t%wS: \n", pCurrAddresses->FriendlyName);

			pUnicast = pCurrAddresses->FirstUnicastAddress;
			if (pUnicast != NULL) {
				for (i = 0; pUnicast != NULL; i++)
					pUnicast = pUnicast->Next;
				//printf("\tNumber of Unicast Addresses: %d\n", i);
				// Parse all IPv4 and addresses
				for (
					IP_ADAPTER_UNICAST_ADDRESS* address = pCurrAddresses->FirstUnicastAddress;
					NULL != address;
					address = address->Next)
				{
					auto family = address->Address.lpSockaddr->sa_family;
					if (AF_INET == family)
					{
						// IPv4
						SOCKADDR_IN* ipv4 = reinterpret_cast<SOCKADDR_IN*>(address->Address.lpSockaddr);

						char str_buffer[22] = { 0 };
						inet_ntop(AF_INET, &(ipv4->sin_addr), str_buffer, 22);
						printf("\t\tUnicast Address: %s\n",str_buffer);

					}
				}
			}
			else
				printf("\tNo Unicast Addresses\n");

			pCurrAddresses = pCurrAddresses->Next;
		}
	}
	else {
		printf("Call to GetAdaptersAddresses failed with error: %d\n",
			dwRetVal);
		if (dwRetVal == ERROR_NO_DATA)
			printf("\tNo addresses were found for the requested parameters\n");
		else {

			if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
				FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL, dwRetVal, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				// Default language
				(LPTSTR)& lpMsgBuf, 0, NULL)) {
				printf("\tError: %s", lpMsgBuf);
				LocalFree(lpMsgBuf);
				if (pAddresses)
					FREE(pAddresses);
				exit(1);
			}
		}
	}

	if (pAddresses) {
		FREE(pAddresses);
	}
}
