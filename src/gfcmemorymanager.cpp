#include "gfcmemorymanager.h"
#include <fstream>
#include <string.h>

#ifdef WIN32
#include <windows.h>
#endif



#if HAVE_SYS_PSTAT_H
# include <sys/pstat.h>
 #endif

#if HAVE_SYS_SYSMP_H
# include <sys/sysmp.h>
#endif

#if HAVE_SYS_SYSINFO_H && HAVE_MACHINE_HAL_SYSINFO_H
# include <sys/sysinfo.h>
# include <machine/hal_sysinfo.h>
 #endif
 
 #if HAVE_SYS_TABLE_H
 # include <sys/table.h>
 #endif
 
 #include <sys/types.h>
 
 #if HAVE_SYS_PARAM_H
 # include <sys/param.h>
 #endif

 

 #ifdef __APPLE__

# include <sys/sysctl.h>
#include <dirent.h>
#include <unistd.h>
 #include <mach/mach_host.h>
 #include <mach/mach_types.h>
 #include <mach/mach.h>
 #include <sys/types.h> 
#endif
 
 #if HAVE_SYS_SYSTEMCFG_H
 # include <sys/systemcfg.h>
 #endif


gfcMemoryManager memoryManager;

gfcMemoryManager::gfcMemoryManager() {
#ifdef linux
percentageToUse=85;
#else
percentageToUse=95;
#endif
    
    
    freeRAM=0; 
    totalRAM=0;
    m_withinLimits=true;
}

#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))

gfcMemoryManager::~gfcMemoryManager() {

}

/**
 * Determines if we are still within the limits of RAM usage that we have established, calculated as a percent of total free RAM. The calling function should know what to do when
  we are outside the limits, probably stop everything that is memory consuming, like loading frames or generating textures.
 * @return 1 if all is good, 0 if we are outside the limits.
 */
int gfcMemoryManager::withinLimits() {
    
    boost::mutex::scoped_lock lock(updateMutex);
    updateData();
    return m_withinLimits;
}

void gfcMemoryManager::setLimit(const float percentage) {
    percentageToUse=percentage;
}



float gfcMemoryManager::getFreeRAM() {
    boost::mutex::scoped_lock lock(updateMutex);
    updateData();
    return freeRAM;
}

float gfcMemoryManager::getTotalRAM() {
    boost::mutex::scoped_lock lock(updateMutex);
    updateData();
    return totalRAM;
}

/**
 * Updates the internal members with the correct memory statistics. 
 On linux it parses the /proc/meminfo file to find the MemTotal stats, and the MemFree+Cached stats for the total free ram.
 On Windows it uses the GlobalMemoryStatusEx function to query the totalPhysical and totalAvailable memory.
 On Mac... well, have no idea what to do yet.
 */
void gfcMemoryManager::updateData() {
    using namespace std;
#ifdef linux

// http://www.devx.com/tips/Tip/30173
    
    char szTmp[256];
    char szMem[256];

    string sMemFree("MemFree:");
    string sCached("Cached:");
    string sMemTotal("MemTotal:");
    string skB("kB");
    freeRAM=0;
    totalRAM=0;

    std::ifstream meminfo("/proc/meminfo");
 
    if ( ! meminfo.is_open() ) {

        return;
    }

    while ( ! meminfo.eof() ) {
        meminfo.getline( szTmp, 256 );
        string::size_type pos0;
        string s2(szTmp);

        pos0 = s2.find(sMemFree);
        if ( pos0 != string::npos ) { //we found MemFree
            string::size_type pos1  = s2.find(skB);

            if ( pos1 != string::npos ) { //we found kB
                string s3 = s2.substr( pos0 + sMemFree.size(), pos1 - (pos0+sMemFree.size()) );
                strncpy(szMem, s3.c_str(), s3.size() );
                freeRAM +=atof(szMem)/1024.0;
                //printf("Total memFree:%f\n",atof(szMem)/1024.0);
                //return (atoi(szMem)*1024*1024) ;

            }
        }

        pos0 = s2.find(sCached);
        if ( pos0 != string::npos ) { //we found Cached
            //check that it is not swapCached we found
            if (s2.find("SwapCached:")==string::npos) {
                string::size_type pos1  = s2.find(skB);

                if ( pos1 != string::npos ) { //we found kB
                    string s3 = s2.substr( pos0 + sCached.size(), pos1 - (pos0+sCached.size()) );
                    strncpy(szMem, s3.c_str(), s3.size() );
                    freeRAM +=atof(szMem)/1024.0;
                    //printf("Total cached:%f\n",atof(szMem)/1024.0);
                    //return (atoi(szMem)*1024*1024) ;

                }
            }
        }
        
         pos0 = s2.find(sMemTotal);
        if ( pos0 != string::npos ) { //we found MemFree
            string::size_type pos1  = s2.find(skB);

            if ( pos1 != string::npos ) { //we found kB
                string s3 = s2.substr( pos0 + sMemTotal.size(), pos1 - (pos0+sMemTotal.size()) );
                strncpy(szMem, s3.c_str(), s3.size() );
                totalRAM=atof(szMem)/1024.0;
                
                //return (atoi(szMem)*1024*1024) ;

            }
        }
        
        
    }
    
    meminfo.close();

    


#endif

#ifdef WIN32

// http://msdn2.microsoft.com/en-us/library/aa366589.aspx





    MEMORYSTATUSEX statex;

    statex.dwLength = sizeof (statex);

    GlobalMemoryStatusEx (&statex);

    freeRAM=statex.ullAvailPhys/1024.0/1024.0;
    totalRAM=statex.ullTotalPhys/1024.0/1024.0;
    

#endif

#ifdef __APPLE__



	//GET TOTAL PHYSICAL RAM
     { /* This works on *bsd and darwin.  */
     size_t physmem;
      size_t len = sizeof physmem;
       static int mib[2] = { CTL_HW, HW_PHYSMEM };
 
     if (sysctl (mib, ARRAY_SIZE(mib), &physmem, &len, NULL, 0) == 0
         && len == sizeof (physmem)){
		 //printf("physmem: %i\n",physmem);
         totalRAM= (double) physmem/1024.0/1024.0;
		 }
     }
	 
	 
	 
	 { /* This works on *bsd and darwin.  */
    /*   unsigned int usermem;
      size_t len = sizeof usermem;
       static int mib[2] = { CTL_HW, HW_USERMEM };
   
       if (sysctl (mib, ARRAY_SIZE (mib), &usermem, &len, NULL, 0) == 0
           && len == sizeof (usermem))
         freeRAM= (double) usermem/1024.0/1024.0;*/
     }





	//GET TOTAL AVAILABLE MEMORY, calculated from total physical memory minus active, inactive and wired pages.
	uint64_t        phys_mem;  /* bytes        */
    size_t          phys_mem_size  = sizeof(phys_mem);
    int             phys_mem_mib[] = { CTL_HW, HW_MEMSIZE };

    int             pagesize;  /* bytes        */
    size_t          pagesize_size  = sizeof(pagesize);
    int             pagesize_mib[] = { CTL_HW, HW_PAGESIZE };

    uint64_t        pages_used;
    off_t           swapSize;
    off_t           swapUsed;
    vm_statistics_data_t vm_stat;
    unsigned int count = HOST_VM_INFO_COUNT;

    sysctl(phys_mem_mib, 2, &phys_mem, &phys_mem_size, NULL, 0);
    sysctl(pagesize_mib, 2, &pagesize, &pagesize_size, NULL, 0);
    host_statistics(mach_host_self(),HOST_VM_INFO,(host_info_t)&vm_stat,&count);
    if(countInactive){
    pages_used = vm_stat.active_count + vm_stat.inactive_count
                                      + vm_stat.wire_count;
    }
    else
    {
    pages_used = vm_stat.active_count + vm_stat.wire_count;
    }
									  
	freeRAM= ((phys_mem/pagesize) - pages_used)*pagesize/1024.0/1024.0;
	totalRAM=phys_mem/1024.0/1024.0;
  



	
	
    // http://lists.apple.com/archives/Darwin-development/2004/Jul/msg00050.html
    // http://net-snmp.sourceforge.net/wiki/index.php/Memory_HAL

    /*
    Here's a snippet that should point you in the right direction:

    #import <mach/host_info.h>
    #import <mach/mach_host.h>

    #define moniker(x) ( (x) >= 1024 ? ( (x) < 1048576 ? 'M' : 'G' ) : 'K'
    )
    #define truesize(x) ( (x) >= 1024 ? ( (x) < 1048576 ? (x)/1024 :
    (x)/1048576 ) : (x) )

    vm_statistics_data_t page_info;
    vm_size_t pagesize;
    mach_msg_type_number_t count;
    kern_return_t kret;
    ...

    pagesize = 0;
    kret = host_page_size (mach_host_self(), &pagesize);

    // vm stats
    count = HOST_VM_INFO_COUNT;
    kret = host_statistics (mach_host_self(), HOST_VM_INFO,
    (host_info_t)&page_info, &count);
    if (kret == KERN_SUCCESS){
    unsigned int pw, pa, pi, pf;
    float pu;

    // Calc kilobytes
    pw = page_info.wire_count*pagesize / 1024;
    pa = page_info.active_count*pagesize / 1024;
    pi = page_info.inactive_count*pagesize / 1024;
    pf = page_info.free_count*pagesize / 1024;

    pu = pw+pa+pi;

    tmp = [NSString stringWithFormat:
    @"%u%c wired, %u%c active, %u%c inactive, %.2f%c used, %u%c
    free, %d pageouts",
    truesize(pw), moniker(pw), truesize(pa), moniker(pa),
    truesize(pi), moniker(pi),
    truesize(pu), moniker(pu), truesize(pf), moniker(pf),
    page_info.pageouts];
    */
#endif

//      printf("totalRAM:%f\n",totalRAM);
//      printf("freeRAM=%f\n",freeRAM);


if(totalRAM>0.0){
    if( (freeRAM*100.0/totalRAM) > (100.0 - percentageToUse) ) {
    	//printf("Total Free RAM: %.2f\n",freeRAM*100.0/totalRAM);
    	m_withinLimits=1;
    }
    else{
    	printf("No longer in memory limits! %f free/%f total (%.2f available/%.2f allowed))\n",freeRAM,totalRAM,(freeRAM*100.0/totalRAM),(100.0 - percentageToUse));
    	m_withinLimits=0;
    	}
    }
    else{
    m_withinLimits=0;
    }

}

float gfcMemoryManager::getLimit()
{
	return percentageToUse;
}


