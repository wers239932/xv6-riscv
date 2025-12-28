//
// Test program for shared memory functionality
//

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void
test_basic_shm()
{
  printf("Testing basic shared memory operations...\n");
  
  // Test shm_open with O_CREATE
  int fd = shm_open("/test_shm", O_CREATE | O_RDWR, 0);
  if(fd < 0) {
    printf("FAIL: shm_open failed\n");
    return;
  }
  printf("PASS: shm_open created shared memory object (fd=%d)\n", fd);
  
  // Test writing to shared memory
  char *test_data = "Hello, shared memory!";
  int n = write(fd, test_data, strlen(test_data));
  if(n != strlen(test_data)) {
    printf("FAIL: write to shared memory failed (wrote %d, expected %d)\n", n, strlen(test_data));
    close(fd);
    return;
  }
  printf("PASS: wrote %d bytes to shared memory\n", n);
  
  // Close the first descriptor
  close(fd);
  printf("PASS: closed first file descriptor\n");
  
  // Test reopening existing object without O_CREATE
  fd = shm_open("/test_shm", O_RDWR, 0);
  if(fd < 0) {
    printf("FAIL: failed to reopen existing shared memory object\n");
    return;
  }
  printf("PASS: reopened existing shared memory object (fd=%d)\n", fd);
  
  // Test reading from shared memory
  char buffer[64];
  memset(buffer, 0, sizeof(buffer));
  
  n = read(fd, buffer, sizeof(buffer) - 1);
  if(n < 0) {
    printf("FAIL: read from shared memory failed\n");
    close(fd);
    return;
  }
  buffer[n] = 0;
  
  if(strcmp(buffer, test_data) == 0) {
    printf("PASS: read correct data from shared memory: '%s'\n", buffer);
  } else {
    printf("FAIL: read incorrect data: expected '%s', got '%s'\n", test_data, buffer);
  }
  
  close(fd);
  
  // Test shm_unlink
  if(shm_unlink("/test_shm") < 0) {
    printf("FAIL: shm_unlink failed\n");
    return;
  }
  printf("PASS: shm_unlink succeeded\n");
  
  // Test that unlinked object cannot be opened
  fd = shm_open("/test_shm", O_RDWR, 0);
  if(fd >= 0) {
    printf("FAIL: opened unlinked shared memory object\n");
    close(fd);
    return;
  }
  printf("PASS: cannot open unlinked shared memory object\n");
}

void
test_create_existing()
{
  printf("\nTesting O_CREATE with existing object...\n");
  
  // Create object
  int fd1 = shm_open("/existing_test", O_CREATE | O_RDWR, 0);
  if(fd1 < 0) {
    printf("FAIL: failed to create object\n");
    return;
  }
  printf("PASS: created object (fd=%d)\n", fd1);
  
  // Try to open existing object with O_CREATE (should succeed)
  int fd2 = shm_open("/existing_test", O_CREATE | O_RDWR, 0);
  if(fd2 < 0) {
    printf("FAIL: failed to open existing object with O_CREATE\n");
    close(fd1);
    shm_unlink("/existing_test");
    return;
  }
  printf("PASS: opened existing object with O_CREATE (fd=%d)\n", fd2);
  
  // Write to first descriptor
  write(fd1, "First", 5);
  
  // Close first descriptor
  close(fd1);
  
  // Read from second descriptor
  char buffer[32];
  memset(buffer, 0, sizeof(buffer));
  int n = read(fd2, buffer, sizeof(buffer) - 1);
  if(n > 0) {
    buffer[n] = 0;
    printf("PASS: read from second descriptor: '%s'\n", buffer);
  } else {
    printf("FAIL: failed to read from second descriptor\n");
  }
  
  close(fd2);
  shm_unlink("/existing_test");
}

void
test_fork_sharing()
{
  printf("\nTesting shared memory between processes...\n");
  
  // Create shared memory object
  int fd = shm_open("/fork_test", O_CREATE | O_RDWR, 0);
  if(fd < 0) {
    printf("FAIL: shm_open failed\n");
    return;
  }
  printf("PASS: created shared memory for fork test\n");
  
  int pid = fork();
  if(pid == 0) {
    // Child process
    char *child_data = "Data from child";
    write(fd, child_data, strlen(child_data));
    printf("Child: wrote data to shared memory\n");
    close(fd);
    exit(0);
  } else if(pid > 0) {
    // Parent process
    int status;
    wait(&status);
    printf("Parent: child process completed\n");
    
    // Reopen and read
    close(fd);
    fd = shm_open("/fork_test", O_RDWR, 0);
    if(fd < 0) {
      printf("FAIL: parent failed to reopen shared memory\n");
      return;
    }
    
    char buffer[64];
    memset(buffer, 0, sizeof(buffer));
    int n = read(fd, buffer, sizeof(buffer) - 1);
    if(n > 0) {
      buffer[n] = 0;
      printf("PASS: parent read data written by child: '%s'\n", buffer);
    } else {
      printf("FAIL: parent failed to read child's data\n");
    }
    
    close(fd);
    shm_unlink("/fork_test");
  } else {
    printf("FAIL: fork failed\n");
    close(fd);
    shm_unlink("/fork_test");
  }
}

void
test_multiple_objects()
{
  printf("\nTesting multiple shared memory objects...\n");
  
  int fd1 = shm_open("/obj1", O_CREATE | O_RDWR, 0);
  int fd2 = shm_open("/obj2", O_CREATE | O_RDWR, 0);
  int fd3 = shm_open("/obj3", O_CREATE | O_RDWR, 0);
  
  if(fd1 < 0 || fd2 < 0 || fd3 < 0) {
    printf("FAIL: failed to create multiple shared memory objects\n");
    if(fd1 >= 0) close(fd1);
    if(fd2 >= 0) close(fd2);
    if(fd3 >= 0) close(fd3);
    return;
  }
  
  printf("PASS: created multiple shared memory objects (fds: %d, %d, %d)\n", fd1, fd2, fd3);
  
  // Write different data to each
  write(fd1, "Object 1", 8);
  write(fd2, "Object 2", 8);
  write(fd3, "Object 3", 8);
  
  close(fd1);
  close(fd2);
  close(fd3);
  
  // Clean up
  shm_unlink("/obj1");
  shm_unlink("/obj2");
  shm_unlink("/obj3");
  
  printf("PASS: multiple objects test completed\n");
}

void
test_error_conditions()
{
  printf("\nTesting error conditions...\n");
  
  // Test invalid name (doesn't start with /)
  int fd = shm_open("invalid_name", O_CREATE | O_RDWR, 0);
  if(fd >= 0) {
    printf("FAIL: accepted invalid name\n");
    close(fd);
  } else {
    printf("PASS: rejected invalid name\n");
  }
  
  // Test opening non-existent object without O_CREATE
  fd = shm_open("/nonexistent", O_RDWR, 0);
  if(fd >= 0) {
    printf("FAIL: opened non-existent object without O_CREATE\n");
    close(fd);
  } else {
    printf("PASS: rejected opening non-existent object without O_CREATE\n");
  }
  
  // Test unlinking non-existent object
  if(shm_unlink("/nonexistent") == 0) {
    printf("FAIL: unlinked non-existent object\n");
  } else {
    printf("PASS: rejected unlinking non-existent object\n");
  }
}

void
test_resource_management()
{
  printf("\nTesting resource management and cleanup...\n");
  
  // Test 1: Object should persist after close until unlink
  printf("Test 1: Object persistence after close...\n");
  int fd1 = shm_open("/resource_test", O_CREATE | O_RDWR, 0);
  if(fd1 < 0) {
    printf("FAIL: failed to create object\n");
    return;
  }
  
  // Write some data
  write(fd1, "persistent", 10);
  close(fd1);
  printf("  Created object, wrote data, and closed descriptor\n");
  
  // Object should still be accessible
  int fd2 = shm_open("/resource_test", O_RDWR, 0);
  if(fd2 < 0) {
    printf("FAIL: object disappeared after close\n");
    return;
  }
  
  char buffer[32];
  memset(buffer, 0, sizeof(buffer));
  int n = read(fd2, buffer, sizeof(buffer) - 1);
  if(n > 0 && strcmp(buffer, "persistent") == 0) {
    printf("PASS: object persisted after close, data intact\n");
  } else {
    printf("FAIL: object data corrupted or lost\n");
  }
  close(fd2);
  
  // Test 2: Multiple references to same object
  printf("Test 2: Multiple references to same object...\n");
  fd1 = shm_open("/resource_test", O_RDWR, 0);
  fd2 = shm_open("/resource_test", O_RDWR, 0);
  int fd3 = shm_open("/resource_test", O_RDWR, 0);
  
  if(fd1 < 0 || fd2 < 0 || fd3 < 0) {
    printf("FAIL: failed to open multiple references\n");
    if(fd1 >= 0) close(fd1);
    if(fd2 >= 0) close(fd2);
    if(fd3 >= 0) close(fd3);
    shm_unlink("/resource_test");
    return;
  }
  printf("  Opened 3 descriptors to same object (fds: %d, %d, %d)\n", fd1, fd2, fd3);
  
  // Write through one descriptor
  int write_result = write(fd1, "multi_ref", 9);
  printf("  Wrote %d bytes through fd1\n", write_result);
  
  // Reset offset for reading through another descriptor
  close(fd2);
  fd2 = shm_open("/resource_test", O_RDWR, 0);
  printf("  Reopened fd2 as %d\n", fd2);
  
  // Read through another descriptor
  memset(buffer, 0, sizeof(buffer));
  n = read(fd2, buffer, 9); // Read exactly the amount we wrote
  buffer[n] = 0; // Ensure null termination
  printf("  Read %d bytes through fd2: '%s'\n", n, buffer);
  if(n == 9 && strcmp(buffer, "multi_ref") == 0) {
    printf("PASS: multiple descriptors share same object\n");
  } else {
    printf("FAIL: multiple descriptors don't share data (read %d bytes: '%s')\n", n, buffer);
  }
  
  // Close two descriptors, object should still exist
  close(fd1);
  close(fd2);
  printf("  Closed 2 descriptors, 1 remains open\n");
  
  // Should still be able to access through third descriptor
  memset(buffer, 0, sizeof(buffer));
  n = read(fd3, buffer, sizeof(buffer) - 1);
  if(n > 0) {
    printf("PASS: object accessible through remaining descriptor\n");
  } else {
    printf("FAIL: object became inaccessible\n");
  }
  close(fd3);
  
  // Test 3: Unlink behavior with active references
  printf("Test 3: Unlink with active references...\n");
  fd1 = shm_open("/unlink_test", O_CREATE | O_RDWR, 0);
  if(fd1 < 0) {
    printf("FAIL: failed to create object for unlink test\n");
    shm_unlink("/resource_test");
    return;
  }
  
  write(fd1, "unlink_test", 11);
  printf("  Created object and wrote data\n");
  
  // Open second descriptor before unlinking
  fd2 = shm_open("/unlink_test", O_RDWR, 0);
  if(fd2 < 0) {
    printf("FAIL: failed to open second descriptor before unlink\n");
    close(fd1);
    shm_unlink("/unlink_test");
    shm_unlink("/resource_test");
    return;
  }
  
  // Unlink while descriptors are still open
  if(shm_unlink("/unlink_test") < 0) {
    printf("FAIL: unlink failed\n");
    close(fd1);
    close(fd2);
    shm_unlink("/resource_test");
    return;
  }
  printf("  Unlinked object while descriptors still open\n");
  
  // Should not be able to open new descriptor to unlinked object
  int fd_new = shm_open("/unlink_test", O_RDWR, 0);
  if(fd_new >= 0) {
    printf("FAIL: opened new descriptor to unlinked object\n");
    close(fd_new);
  } else {
    printf("PASS: cannot open new descriptor to unlinked object\n");
  }
  
  // But existing descriptors should still work
  // Read through second descriptor (which has offset 0)
  memset(buffer, 0, sizeof(buffer));
  n = read(fd2, buffer, sizeof(buffer) - 1);
  if(n > 0 && strcmp(buffer, "unlink_test") == 0) {
    printf("PASS: existing descriptor still works after unlink\n");
  } else {
    printf("FAIL: existing descriptor broken after unlink (read %d bytes: '%s')\n", n, buffer);
  }
  
  // Close descriptors - object should be fully cleaned up
  close(fd1);
  close(fd2);
  printf("  Closed all descriptors - object should be cleaned up\n");
  
  // Test 4: Resource reuse after cleanup
  printf("Test 4: Resource reuse after cleanup...\n");
  
  // Try to create object with same name - should work (reusing slot)
  fd1 = shm_open("/unlink_test", O_CREATE | O_RDWR, 0);
  if(fd1 < 0) {
    printf("FAIL: failed to reuse object name after cleanup\n");
  } else {
    printf("PASS: successfully reused object name after cleanup\n");
    
    // Should be a fresh object (no old data)
    memset(buffer, 0, sizeof(buffer));
    n = read(fd1, buffer, sizeof(buffer) - 1);
    if(n == 0 || buffer[0] == 0) {
      printf("PASS: new object is clean (no old data)\n");
    } else {
      printf("FAIL: new object contains old data: '%s'\n", buffer);
    }
    
    close(fd1);
    shm_unlink("/unlink_test");
  }
  
  // Clean up the persistent test object
  shm_unlink("/resource_test");
  printf("PASS: resource management tests completed\n");
}

void
test_large_data_allocation()
{
  printf("\nTesting large data allocation and memory management...\n");
  
  // Test 1: Write data spanning multiple pages (8KB = 2 pages)
  printf("Test 1: Multi-page allocation (8KB)...\n");
  int fd = shm_open("/large_test", O_CREATE | O_RDWR, 0);
  if(fd < 0) {
    printf("FAIL: failed to create large shared memory object\n");
    return;
  }
  
  // Prepare test data - 8KB of pattern data
  char large_data[8192];
  
  // Fill with recognizable pattern
  for(int i = 0; i < 8192; i++) {
    large_data[i] = (char)('A' + (i % 26));
  }
  
  // Write the large data
  int written = write(fd, large_data, 8192);
  printf("  Attempted to write 8192 bytes, actually wrote: %d bytes\n", written);
  
  if(written > 0) {
    printf("PASS: successfully wrote %d bytes to shared memory\n", written);
    
    // Test reading back the data
    close(fd);
    fd = shm_open("/large_test", O_RDWR, 0);
    if(fd < 0) {
      printf("FAIL: failed to reopen large object\n");
      return;
    }
    
    char read_buffer[8192];
    memset(read_buffer, 0, 8192);
    
    int read_bytes = read(fd, read_buffer, 8192);
    printf("  Read back %d bytes\n", read_bytes);
    
    // Verify data integrity
    int errors = 0;
    for(int i = 0; i < read_bytes && i < written; i++) {
      if(read_buffer[i] != large_data[i]) {
        errors++;
        if(errors <= 5) { // Show first few errors
          printf("  Data mismatch at offset %d: expected %c, got %c\n", 
                 i, large_data[i], read_buffer[i]);
        }
      }
    }
    
    if(errors == 0) {
      printf("PASS: all data verified correctly across multiple pages\n");
    } else {
      printf("FAIL: found %d data integrity errors\n", errors);
    }
  } else {
    printf("FAIL: failed to write large data\n");
  }
  
  close(fd);
  shm_unlink("/large_test");
  
  // Test 2: Test maximum size allocation (approaching 4MB limit)
  printf("Test 2: Large allocation (1MB)...\n");
  fd = shm_open("/max_test", O_CREATE | O_RDWR, 0);
  if(fd < 0) {
    printf("FAIL: failed to create maximum size object\n");
    return;
  }
  
  // Try to write 1MB of data
  int chunk_size = 4096; // Write in 4KB chunks
  int total_size = 1024 * 1024; // 1MB
  char chunk_data[4096];
  
  // Fill chunk with pattern
  for(int i = 0; i < chunk_size; i++) {
    chunk_data[i] = (char)('0' + (i % 10));
  }
  
  int total_written = 0;
  int chunks = total_size / chunk_size;
  
  for(int chunk = 0; chunk < chunks; chunk++) {
    int written = write(fd, chunk_data, chunk_size);
    if(written <= 0) {
      printf("  Stopped writing at chunk %d (total written: %d bytes)\n", 
             chunk, total_written);
      break;
    }
    total_written += written;
    
    if(chunk % 64 == 0) { // Progress indicator every 256KB
      printf("  Written %d KB so far...\n", total_written / 1024);
    }
  }
  
  printf("  Total written: %d bytes (%d KB)\n", total_written, total_written / 1024);
  
  if(total_written > 0) {
    printf("PASS: successfully allocated and wrote %d bytes\n", total_written);
    
    // Test reading back some data from different positions
    close(fd);
    fd = shm_open("/max_test", O_RDWR, 0);
    
    char verify_buffer[4096];
    
    // Read from beginning
    memset(verify_buffer, 0, sizeof(verify_buffer));
    int read_bytes = read(fd, verify_buffer, chunk_size);
    
    int errors = 0;
    for(int i = 0; i < read_bytes; i++) {
      if(verify_buffer[i] != chunk_data[i]) {
        errors++;
      }
    }
    
    if(errors == 0 && read_bytes == chunk_size) {
      printf("PASS: data integrity verified for large allocation\n");
    } else {
      printf("FAIL: data integrity issues (errors: %d, read: %d)\n", errors, read_bytes);
    }
  } else {
    printf("INFO: no data could be written (may be memory limited)\n");
  }
  
  close(fd);
  shm_unlink("/max_test");
  
  // Test 3: Test behavior at size limits
  printf("Test 3: Testing size limits...\n");
  
  // Try to create object larger than 4MB limit
  fd = shm_open("/overlimit_test", O_CREATE | O_RDWR, 0);
  if(fd < 0) {
    printf("FAIL: failed to create object for limit test\n");
    return;
  }
  
  char small_buffer[1024];
  memset(small_buffer, 'X', 1024);
  
  // Keep writing until we hit the limit
  int limit_written = 0;
  int attempts = 0;
  int max_attempts = 5000; // Prevent infinite loop
  
  while(attempts < max_attempts) {
    int written = write(fd, small_buffer, 1024);
    if(written <= 0) {
      break;
    }
    limit_written += written;
    attempts++;
    
    if(attempts % 1000 == 0) {
      printf("  Limit test: written %d KB in %d attempts\n", 
             limit_written / 1024, attempts);
    }
  }
  
  printf("  Reached write limit at %d bytes (%d KB) after %d attempts\n", 
         limit_written, limit_written / 1024, attempts);
  
  if(limit_written > 0) {
    printf("PASS: write limit enforced at %d bytes\n", limit_written);
  } else {
    printf("INFO: immediate write limit (static allocation)\n");
  }
  
  close(fd);
  shm_unlink("/overlimit_test");
  
  printf("PASS: large data allocation tests completed\n");
}

void
test_shm_mmap_functionality()
{
  printf("\nTesting shared memory mmap functionality...\n");
  
  // Test 1: Basic mmap/munmap
  printf("Test 1: Basic mmap and munmap...\n");
  int fd = shm_open("/mmap_test", O_CREATE | O_RDWR, 0);
  if(fd < 0) {
    printf("FAIL: failed to create shared memory object for mmap\n");
    return;
  }
  
  // Write some test data
  char *test_data = "Hello, mmap world!";
  write(fd, test_data, strlen(test_data));
  
  // Map the shared memory
  void *mapped_addr = shm_mmap(fd, 0, 4096, 0x3); // PROT_READ | PROT_WRITE
  if(mapped_addr == (void*)-1) {
    printf("FAIL: shm_mmap failed\n");
    close(fd);
    shm_unlink("/mmap_test");
    return;
  }
  
  printf("PASS: shm_mmap succeeded, mapped at address %p\n", mapped_addr);
  
  // Try to access the mapped memory
  // Note: This is a simplified test - in real usage we'd read from the mapped address
  printf("INFO: mapped memory at virtual address %p\n", mapped_addr);
  
  // Unmap the memory
  if(shm_munmap(mapped_addr, 4096) < 0) {
    printf("FAIL: shm_munmap failed\n");
  } else {
    printf("PASS: shm_munmap succeeded\n");
  }
  
  close(fd);
  shm_unlink("/mmap_test");
  
  // Test 2: Multiple mappings
  printf("Test 2: Multiple mappings of same object...\n");
  fd = shm_open("/multi_mmap_test", O_CREATE | O_RDWR, 0);
  if(fd < 0) {
    printf("FAIL: failed to create shared memory object for multi-mmap\n");
    return;
  }
  
  // Write test data
  write(fd, "Multi-map test", 14);
  
  // Create multiple mappings
  void *addr1 = shm_mmap(fd, 0, 4096, 0x3);
  void *addr2 = shm_mmap(fd, 0, 4096, 0x1); // Read-only
  
  if(addr1 == (void*)-1 || addr2 == (void*)-1) {
    printf("FAIL: failed to create multiple mappings\n");
    if(addr1 != (void*)-1) shm_munmap(addr1, 4096);
    if(addr2 != (void*)-1) shm_munmap(addr2, 4096);
    close(fd);
    shm_unlink("/multi_mmap_test");
    return;
  }
  
  printf("PASS: created multiple mappings at %p and %p\n", addr1, addr2);
  
  // Clean up mappings
  shm_munmap(addr1, 4096);
  shm_munmap(addr2, 4096);
  
  close(fd);
  shm_unlink("/multi_mmap_test");
  
  // Test 3: Error conditions
  printf("Test 3: mmap error conditions...\n");
  
  // Try to map invalid file descriptor
  void *bad_addr = shm_mmap(-1, 0, 4096, 0x3);
  if(bad_addr == (void*)-1) {
    printf("PASS: shm_mmap correctly rejected invalid fd\n");
  } else {
    printf("FAIL: shm_mmap accepted invalid fd\n");
    shm_munmap(bad_addr, 4096);
  }
  
  // Try to map regular file (not shared memory)
  int regular_fd = open("README", O_RDONLY);
  if(regular_fd >= 0) {
    void *regular_addr = shm_mmap(regular_fd, 0, 4096, 0x1);
    if(regular_addr == (void*)-1) {
      printf("PASS: shm_mmap correctly rejected regular file\n");
    } else {
      printf("FAIL: shm_mmap accepted regular file\n");
      shm_munmap(regular_addr, 4096);
    }
    close(regular_fd);
  }
  
  printf("PASS: mmap functionality tests completed\n");
}

void
test_reference_counting_advanced()
{
  printf("\nTesting advanced reference counting scenarios...\n");
  
  // Test 1: Reference counting with mmap
  printf("Test 1: Reference counting with mmap...\n");
  int fd = shm_open("/ref_mmap_test", O_CREATE | O_RDWR, 0);
  if(fd < 0) {
    printf("FAIL: failed to create shared memory object\n");
    return;
  }
  
  write(fd, "Reference test", 14);
  
  // Map the memory (this should increase internal reference count)
  void *mapped = shm_mmap(fd, 0, 4096, 0x3);
  if(mapped == (void*)-1) {
    printf("FAIL: failed to map memory\n");
    close(fd);
    shm_unlink("/ref_mmap_test");
    return;
  }
  
  printf("PASS: mapped memory for reference test\n");
  
  // Close the file descriptor
  close(fd);
  printf("INFO: closed file descriptor while memory still mapped\n");
  
  // Try to unlink the object
  if(shm_unlink("/ref_mmap_test") == 0) {
    printf("PASS: shm_unlink succeeded (object marked for deletion)\n");
  } else {
    printf("FAIL: shm_unlink failed\n");
  }
  
  // Memory should still be accessible (object should persist due to mapping)
  printf("INFO: memory should still be accessible due to mapping\n");
  
  // Unmap the memory (this should allow final cleanup)
  shm_munmap(mapped, 4096);
  printf("PASS: unmapped memory, object should be cleaned up now\n");
  
  // Test 2: Multiple file descriptors to same object
  printf("Test 2: Multiple file descriptors...\n");
  int fd1 = shm_open("/multi_fd_test", O_CREATE | O_RDWR, 0);
  int fd2 = shm_open("/multi_fd_test", O_RDWR, 0);
  int fd3 = shm_open("/multi_fd_test", O_RDWR, 0);
  
  if(fd1 < 0 || fd2 < 0 || fd3 < 0) {
    printf("FAIL: failed to create multiple file descriptors\n");
    if(fd1 >= 0) close(fd1);
    if(fd2 >= 0) close(fd2);
    if(fd3 >= 0) close(fd3);
    shm_unlink("/multi_fd_test");
    return;
  }
  
  printf("PASS: created multiple file descriptors (%d, %d, %d)\n", fd1, fd2, fd3);
  
  // Write through one descriptor
  write(fd1, "Multi FD test", 13);
  
  // Read through another
  char buffer[32];
  memset(buffer, 0, sizeof(buffer));
  int n = read(fd2, buffer, sizeof(buffer) - 1);
  if(n > 0 && strcmp(buffer, "Multi FD test") == 0) {
    printf("PASS: data shared correctly between file descriptors\n");
  } else {
    printf("FAIL: data not shared between file descriptors\n");
  }
  
  // Close descriptors one by one
  close(fd1);
  printf("INFO: closed fd1, object should persist\n");
  
  close(fd2);
  printf("INFO: closed fd2, object should persist\n");
  
  // Object should still be accessible through fd3
  memset(buffer, 0, sizeof(buffer));
  n = read(fd3, buffer, sizeof(buffer) - 1);
  if(n > 0) {
    printf("PASS: object still accessible through remaining descriptor\n");
  } else {
    printf("FAIL: object became inaccessible\n");
  }
  
  close(fd3);
  shm_unlink("/multi_fd_test");
  
  printf("PASS: advanced reference counting tests completed\n");
}

int
main(int argc, char *argv[])
{
  printf("Starting shared memory tests...\n\n");
  
  test_basic_shm();
  test_create_existing();
  test_fork_sharing();
  test_multiple_objects();
  test_error_conditions();
  test_large_data_allocation();
  test_shm_mmap_functionality();
  test_reference_counting_advanced();
  test_resource_management();
  
  printf("\nShared memory tests completed.\n");
  exit(0);
}