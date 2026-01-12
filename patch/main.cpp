#include <iostream>
#include <cstdlib>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <sys/syscall.h>
#include "include/kpimg_enc.h"
#include "include/kptools_linux.h"
#include "include/kptools_android.h"
#include "include/header.h"

#if !defined(__NR_memfd_create)
#if __ANDROID_API__ < 29
#define __NR_memfd_create 279
#endif
#endif

namespace fs = std::filesystem;

void decrypt_kpimg(std::vector<uint8_t>& data) {
    for (auto& b : data) b ^= 0xAA;  // Дешифровка с использованием XOR
}

int create_memfd(const std::vector<uint8_t>& data) {
    int fd = syscall(__NR_memfd_create, "kptools_memfd", 0);  // Создание мемори-файла
    if (fd == -1) return -1;

    if (write(fd, data.data(), data.size()) != static_cast<ssize_t>(data.size())) {
        close(fd);
        return -1;
    }
    
    lseek(fd, 0, SEEK_SET);  // Перемещаем указатель на начало
    return fd;
}

void execute_in_memory(const std::vector<uint8_t>& data, const std::vector<std::string>& args) {
    int mem_fd = create_memfd(data);
    if (mem_fd == -1) {
        throw std::runtime_error("Unable to create memory file descriptor");
    }

    std::vector<char*> argv;
    argv.push_back(strdup("kptools_mem"));
    for (const auto& arg : args) {
        argv.push_back(strdup(arg.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid = fork();  // Создаем новый процесс
    if (pid == 0) {
        fexecve(mem_fd, argv.data(), environ);  // Выполняем программу из памяти
        _exit(EXIT_FAILURE);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);  // Ожидаем завершения процесса
    } else {
        throw std::runtime_error("Fork failed");
    }

    close(mem_fd);
    for (auto& arg : argv) free(arg);  // Освобождаем память
}

void check_file_exists(const std::string& file_path) {
    if (!fs::exists(file_path)) {
        std::cerr << "Error: File " << file_path << " does not exist" << std::endl;
        exit(EXIT_FAILURE);
    }
}

std::string fd_to_path(int fd) {
    return "/proc/self/fd/" + std::to_string(fd);  // Возвращаем путь к файлу, ассоциированному с дескриптором
}

int main() try {
    check_file_exists("Image");  // Проверяем, существует ли файл Image

    // Определяем какие данные использовать в зависимости от архитектуры
    #ifdef __aarch64__
        std::vector<uint8_t> kptools_data(res_kptools_android, res_kptools_android + res_kptools_android_len);
    #else
        std::vector<uint8_t> kptools_data(res_kptools_linux, res_kptools_linux + res_kptools_linux_len);
    #endif

    // Данные для образа
    std::vector<uint8_t> kpimg_data(res_kpimg_enc, res_kpimg_enc + res_kpimg_enc_len);
    
    // Дешифруем образ
    decrypt_kpimg(kpimg_data);
    
    // Создаем мемори-файл для образа
    int kpimg_mem_fd = create_memfd(kpimg_data); 
    std::string kpimg_path = fd_to_path(kpimg_mem_fd);  // Получаем путь к этому файлу

    // Аргументы для запуска kptools
    std::vector<std::string> args = {
        "-p",  // Параметр для kptools
        "-s", "123",  // Еще один параметр
        "-i", "Image",  // Входной файл
        "-k", kpimg_path,  // Путь к образу
        "-o", "oImage"  // Выходной файл
    };

    // Выполняем kptools в памяти
    execute_in_memory(kptools_data, args);

    close(kpimg_mem_fd);  // Закрываем дескриптор файла
    return 0;
}
catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return EXIT_FAILURE;
}
