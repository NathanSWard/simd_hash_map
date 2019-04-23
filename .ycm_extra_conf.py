def Settings( **kwargs ):
    return {
        'flags': [ '-x', 'c++', '-std=c++17', '-Wall', '-Wpedantic', '-mavx512f', '-mavx512bw' ],       
    }
