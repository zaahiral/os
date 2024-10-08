//@globals
//@body
    auto file = Filesystem::VirtualFilesystemManager::instance().open_file("initrd/dir/afile.txt", 0);
    auto fileLength = Filesystem::VirtualFilesystemManager::instance().filelen(file);
    // Allocate a buffer to copy the contents of the file into.
    // We add one to the length to add a null terminator.
    auto fileBuffer = (char*) kmalloc(fileLength + 1, 0);
    Filesystem::VirtualFilesystemManager::instance().read(file, 0, fileLength, fileBuffer);
    // Add null terminator to string.
    fileBuffer[fileLength] = '\0';
    printf("File in directory contents: ");
    printf(fileBuffer);
    while (1) asm volatile("hlt");
