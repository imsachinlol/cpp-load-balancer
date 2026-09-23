#!/bin/bash

LB_URL="http://127.0.0.1:8080/"

echo "========================================"
echo " C++ LOAD BALANCER BENCHMARK"
echo "========================================"
echo

echo "Test 1: 1 thread, 10 connections, 10 seconds"
echo "----------------------------------------"
wrk -t1 -c10 -d10s -H "Connection: close" "$LB_URL"

echo
echo "Test 2: 2 threads, 50 connections, 10 seconds"
echo "----------------------------------------"
wrk -t2 -c50 -d10s -H "Connection: close" "$LB_URL"

echo
echo "Test 3: 4 threads, 100 connections, 10 seconds"
echo "----------------------------------------"
wrk -t4 -c100 -d10s -H "Connection: close" "$LB_URL"

echo
echo "Test 4: 4 threads, 200 connections, 10 seconds"
echo "----------------------------------------"
wrk -t4 -c200 -d10s -H "Connection: close" "$LB_URL"

echo
echo "========================================"
echo " BENCHMARK COMPLETE"
echo "========================================"
